#include "passfwd.h"

#include "table.h"
#include "types.h"
#include "constraints.h"
#include "scope.h"
#include "unify.h"
#include "import.h"

#include "passes.h"
#include "passzero.h"
#include "passbody.h"
#include "autointf.h"

static STEP_NM(step_codeloc_for_generated,
               -1);
static error step_codeloc_for_generated(struct module *mod, struct node *node,
                                        void *user, bool *stop) {
  if (node->codeloc.pos == 0) {
    node->codeloc = parent(node)->codeloc;
  }

  return 0;
}

static STEP_NM(step_export_pre_post_invariant,
               NM(PRE) | NM(POST) | NM(INVARIANT));
static error step_export_pre_post_invariant(struct module *mod, struct node *node,
                                            void *user, bool *stop) {
  const struct node *par = parent_const(node);
  if (par->which == DEFTYPE || par->which == DEFINTF) {
    node_toplevel(node)->flags |= node_toplevel_const(par)->flags & TOP_IS_EXPORT;
    return 0;
  }

  if (par->which == BLOCK) {
    const struct node *pparent = parent_const(par);
    if (pparent->which == DEFFUN || pparent->which == DEFMETHOD) {
      node_toplevel(node)->flags |= node_toplevel_const(pparent)->flags & TOP_IS_EXPORT;
    }
  }

  return 0;
}

STEP_NM(step_stop_already_morningtypepass,
        NM(LET) | NM(ISA) | NM(GENARGS) | NM(FUNARGS) |
        NM(DEFGENARG) | NM(SETGENARG) | NM(DEFFIELD) | NM(DEFCHOICE) |
        NM(WITHIN));
error step_stop_already_morningtypepass(struct module *mod, struct node *node,
                                        void *user, bool *stop) {
  DSTEP(mod, node);

  *stop = node->typ != NULL;

  return 0;
}

static error pass_early_typing(struct module *mod, struct node *root,
                               void *user, ssize_t shallow_last_up) {
  PASS(
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_block);
    DOWN_STEP(step_stop_already_morningtypepass);
    DOWN_STEP(step_type_destruct_mark);
    ,
    UP_STEP(step_type_inference);
    UP_STEP(step_constraint_inference);
    UP_STEP(step_remove_typeconstraints);
    );
  return 0;
}

static error morningtypepass(struct module *mod, struct node *node) {
  PUSH_STATE(mod->state->step_state);
  bool tentatively_saved = mod->state->tentatively;
  if (mod->state->prev != NULL) {
    mod->state->tentatively |= mod->state->prev->tentatively;
  }

  error e = pass_early_typing(mod, node, NULL, -1);
  EXCEPT(e);

  mod->state->tentatively = tentatively_saved;
  POP_STATE(mod->state->step_state);

  return 0;
}

static STEP_NM(step_type_definitions,
               STEP_NM_DEFS);
static error step_type_definitions(struct module *mod, struct node *node,
                                   void *user, bool *stop) {
  DSTEP(mod, node);

  ident id = node_ident(subs_first(node));

  if (subs_count_atleast(subs_at(node, IDX_GENARGS), 1)
      && node_toplevel(node)->generic->our_generic_functor_typ != NULL) {
    set_typ(&node->typ, typ_create(NULL, node));
  } else if (mod->path[0] == ID_NLANG
             && (id >= ID_TBI__FIRST && id <= ID_TBI__LAST)) {
    // FIXME Effectively reserving these idents for builtin types, but
    // that's a temporary trick to avoid having to look up the current
    // module path.
    set_typ(&node->typ, typ_create(mod->gctx->builtin_typs_by_name[id], node));
  } else {
    set_typ(&node->typ, typ_create(NULL, node));
  }
  node->flags = NODE_IS_TYPE;

  step_constraint_inference(mod, node, NULL, NULL);

  return 0;
}

static struct node *do_move_detached_member(struct module *mod,
                                            struct node *par,
                                            struct node *node) {
  struct node *nxt = next(node);
  struct toplevel *toplevel = node_toplevel(node);
  if (toplevel->scope_name == 0) {
    return nxt;
  }
  if (toplevel->generic->our_generic_functor_typ != NULL) {
    assert(parent(node)->which == DEFTYPE);
    return nxt;
  }

  struct node *container = NULL;
  error e = scope_lookup_ident_wontimport(&container, node, mod,
                                          &parent(node)->scope,
                                          toplevel->scope_name, false);
  assert(!e);
  assert(container->which == DEFTYPE);

  toplevel->scope_name = 0;

  node_subs_remove(par, node);
  node_subs_append(container, node);

  struct toplevel *container_toplevel = node_toplevel(container);
  if (container_toplevel->generic != NULL) {
    struct node *copy = node_new_subnode(
      mod, container_toplevel->generic->instances[0]);
    node_deepcopy(mod, copy, toplevel->generic->instances[0]);
  } else {
    const struct node *genargs = subs_at_const(node, IDX_GENARGS);
    if (!subs_count_atleast(genargs, 1)) {
      // Remove uneeded 'generic', added in step_generics_pristine_copy().
      free(toplevel->generic->instances);
      free(toplevel->generic);
      toplevel->generic = NULL;
    }
  }

  if (toplevel->generic != NULL
      || container_toplevel->generic != NULL) {
    // FIXME: This is not strictly correct, but for now, it is necessary to
    // assume that all methods and functions in a generic type are 'inline'.
    toplevel->flags |= TOP_IS_INLINE;
    if (toplevel->generic != NULL) {
      node_toplevel(toplevel->generic->instances[0])->flags |= TOP_IS_INLINE;
    }
  }

  return nxt;
}

static STEP_NM(step_move_detached_members,
               NM(MODULE_BODY));
static error step_move_detached_members(struct module *mod, struct node *node,
                                        void *user, bool *stop) {
  DSTEP(mod, node);

  for (struct node *s = subs_first(node); s != NULL;) {
    switch (s->which) {
    case DEFFUN:
    case DEFMETHOD:
      s = do_move_detached_member(mod, node, s);
      break;
    default:
      s = next(s);
      break;
    }
  }

  return 0;
}

static STEP_NM(step_lexical_import,
               NM(IMPORT));
static error step_lexical_import(struct module *mod, struct node *node,
                                 void *user, bool *stop) {
  DSTEP(mod, node);
  error e;

  if (node_is_at_top(node)) {
    e = lexical_import(&mod->body->scope, mod, node, node);
    EXCEPT(e);
  }

  return 0;
}

static error lexical_retval(struct module *mod, struct node *fun, struct node *retval) {
  error e;

  switch (retval->which) {
  case BIN:
  case UN:
  case IDENT:
  case CALL:
    break;
  case DEFARG:
    e = scope_define(mod, &fun->scope, subs_first(retval), retval);
    EXCEPT(e);
    if (subs_last(retval)->which == TUPLE) {
      e = lexical_retval(mod, fun, subs_last(retval));
      EXCEPT(e);
    }
    break;
  case TUPLE:
    FOREACH_SUB(r, retval) {
      e = lexical_retval(mod, fun, r);
      EXCEPT(e);
    }
    break;
  default:
    e = mk_except(mod, retval, "return value type expression not supported");
    EXCEPT(e);
  }

  return 0;
}

static struct scope *find_scope_for_name(struct node *node) {
  assert(node->which == DEFNAME || node->which == DEFALIAS);

  if (node_ident(node) == ID_OTHERWISE) {
    return NULL;
  } else if (node->as.DEFNAME.is_globalenv) {
    assert(parent(parent(node))->which == MODULE_BODY);
    return &parent(parent(node))->as.MODULE_BODY.globalenv_scope;
  } else if (NM(parent(parent(node))->which)
             & (STEP_NM_DEFS_NO_FUNS | NM(MODULE_BODY))) {
    return &parent(parent(node))->scope;
  } else {
    struct node *p = node;
    do {
      p = parent(p);
    } while (p->which != BLOCK || p->as.BLOCK.is_scopeless);
    return &p->scope;
  }
}

static STEP_NM(step_lexical_scoping,
               NM(DEFFUN) | NM(DEFMETHOD) | NM(DEFTYPE) | NM(DEFINTF) |
               NM(DEFFIELD) | NM(DEFCHOICE) | NM(DEFALIAS) | NM(DEFNAME) |
               NM(WITHIN));
static error step_lexical_scoping(struct module *mod, struct node *node,
                                  void *user, bool *stop) {
  DSTEP(mod, node);
  struct node *id = NULL;
  struct scope *sc = NULL;
  const struct toplevel *toplevel = NULL;
  error e;
  struct node *par = parent(node);

  switch (node->which) {
  case DEFFUN:
  case DEFMETHOD:
    id = subs_first(node);
    if (id->which != IDENT) {
      assert(id->which == BIN);
      id = subs_last(id);
    }

    toplevel = node_toplevel_const(node);
    if (toplevel->generic->our_generic_functor_typ != NULL) {
      // See comment below for DEFTYPE/DEFINTF. To get the same instance
      // than the current function, you have to explicitly instantiate it
      // (as there is no 'thisfun').
      sc = NULL;
    } else if (toplevel->scope_name != 0) {
      struct node *container = NULL;
      error e = scope_lookup_ident_wontimport(&container, node, mod,
                                              &parent(node)->scope,
                                              toplevel->scope_name, false);
      EXCEPT(e);
      assert(container->which == DEFTYPE);

      sc = &container->scope;
    } else {
      sc = &par->scope;
    }
    break;
  case DEFTYPE:
  case DEFINTF:
    toplevel = node_toplevel_const(node);
    if (toplevel->generic != NULL
        && toplevel->generic->our_generic_functor_typ != NULL) {
      // For generic instances, do not define the name, as we want the type
      // name to point to the generic functor (e.g. in '(vector u8)', allow
      // 'vector' to be used in '(vector u16)'. To get the current instance,
      // use this or final.
      sc = NULL;
    } else {
      id = subs_first(node);
      sc = &parent(node)->scope;
    }
    break;
  case DEFFIELD:
    id = subs_first(node);
    sc = &parent(node)->scope;
    break;
  case DEFCHOICE:
    id = subs_first(node);
    struct node *p = par;
    while (p->which == DEFCHOICE) {
      p = parent(p);
    }
    sc = &p->scope;
    break;
  case DEFNAME:
  case DEFALIAS:
    id = subs_first(node);
    sc = find_scope_for_name(node);
    break;
  case WITHIN:
    if (subs_count_atleast(node, 1)
        && subs_first(node)->which != WITHIN) {
      struct node *n = subs_first(node);
      if (n->which == BIN) {
        assert(n->as.BIN.operator == TDOT);
        id = subs_last(n);
      } else {
        id = n;
      }

      struct node *pparent = parent(par);
      if (pparent->which == MODULE_BODY) {
        sc = &pparent->scope;
      } else {
        assert(pparent->which == DEFFUN || pparent->which == DEFMETHOD);
        sc = &subs_last(parent(par))->scope;
      }
    }
    break;
  default:
    assert(false && "Unreached");
    return 0;
  }

  if (sc != NULL) {
    e = scope_define(mod, sc, id, node);
    EXCEPT(e);
  }

  struct node *genargs = NULL;
  switch (node->which) {
  case DEFTYPE:
  case DEFINTF:
    genargs = subs_at(node, IDX_GENARGS);
    FOREACH_SUB(ga, genargs) {
      assert(ga->which == DEFGENARG || ga->which == SETGENARG);
      e = scope_define(mod, &node->scope, subs_first(ga), ga);
      EXCEPT(e);
    }
    break;
  case DEFFUN:
  case DEFMETHOD:
    genargs = subs_at(node, IDX_GENARGS);
    FOREACH_SUB(ga, genargs) {
      assert(ga->which == DEFGENARG || ga->which == SETGENARG);
      e = scope_define(mod, &node->scope, subs_first(ga), ga);
      EXCEPT(e);
    }

    struct node *funargs = subs_at(node, IDX_FUNARGS);
    FOREACH_SUB(arg, funargs) {
      if (next_const(arg) == NULL) {
        break;
      }
      assert(arg->which == DEFARG);
      e = scope_define(mod, &node->scope, subs_first(arg), arg);
      EXCEPT(e);
    }

    e = lexical_retval(mod, node, node_fun_retval(node));
    EXCEPT(e);
    break;
  default:
    break;
  }

  return 0;
}

static error define_builtin_alias(struct module *mod, struct node *node,
                                  ident name, struct typ *t) {
  struct node *let = mk_node(mod, node, LET);
  node_subs_remove(node, let);
  node_subs_insert_after(node, subs_at(node, 2), let);
  let->flags = NODE_IS_GLOBAL_LET;
  struct node *defa = mk_node(mod, let, DEFALIAS);
  struct node *n = mk_node(mod, defa, IDENT);
  n->as.IDENT.name = name;
  struct node *expr = mk_node(mod, defa, DIRECTDEF);
  set_typ(&expr->as.DIRECTDEF.typ, t);
  expr->as.DIRECTDEF.flags = NODE_IS_TYPE;

  error e = catchup(mod, NULL, let, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);
  return 0;
}

static STEP_NM(step_add_builtin_members,
               NM(DEFTYPE) | NM(DEFINTF));
static error step_add_builtin_members(struct module *mod, struct node *node,
                                      void *user, bool *stop) {
  DSTEP(mod, node);

  if (typ_is_pseudo_builtin(node->typ)) {
    return 0;
  }

  define_builtin_alias(mod, node, ID_THIS, node->typ);
  define_builtin_alias(mod, node, ID_FINAL, node->typ);

  return 0;
}

static STEP_NM(step_add_builtin_members_enum_union,
               NM(DEFTYPE));
static error step_add_builtin_members_enum_union(struct module *mod, struct node *node,
                                                 void *user, bool *stop) {
  DSTEP(mod, node);

  if (node->as.DEFTYPE.kind != DEFTYPE_ENUM
      && node->as.DEFTYPE.kind != DEFTYPE_UNION) {
    return 0;
  }

  define_builtin_alias(mod, node, ID_TAG_TYPE, node->as.DEFTYPE.tag_typ);

  return 0;
}

static STEP_NM(step_type_inference_genargs,
               STEP_NM_DEFS);
static error step_type_inference_genargs(struct module *mod, struct node *node,
                                         void *user, bool *stop) {
  DSTEP(mod, node);
  error e;

  if (node->typ == TBI__NOT_TYPEABLE) {
    return 0;
  }

  struct node *genargs = subs_at(node, IDX_GENARGS);
  e = morningtypepass(mod, genargs);
  EXCEPT(e);

  return 0;
}

static STEP_NM(step_type_aliases,
               NM(LET));
static error step_type_aliases(struct module *mod, struct node *node,
                            void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *par = parent(node);
  if ((node_is_at_top(node) || node_is_at_top(par))
      && subs_first(node)->which == DEFALIAS) {
    error e = morningtypepass(mod, node);
    EXCEPT(e);
  }

  return 0;
}

static STEP_NM(step_type_create_update,
               STEP_NM_DEFS);
static error step_type_create_update(struct module *mod, struct node *node,
                                     void *user, bool *stop) {
  DSTEP(mod, node);

  typ_create_update_genargs(node->typ);
  typ_create_update_hash(node->typ);

  return 0;
}

static STEP_NM(step_type_inference_isalist,
               NM(ISA));
static error step_type_inference_isalist(struct module *mod, struct node *node,
                                         void *user, bool *stop) {
  DSTEP(mod, node);

  error e = morningtypepass(mod, node);
  EXCEPT(e);

  return 0;
}

static STEP_NM(step_type_update_quickisa,
               STEP_NM_DEFS_NO_FUNS);
static error step_type_update_quickisa(struct module *mod, struct node *node,
                                       void *user, bool *stop) {
  DSTEP(mod, node);

  typ_create_update_quickisa(node->typ);

  return 0;
}

static error validate_genarg_types(struct module *mod, struct node *node) {
  struct node *genargs = subs_at(node, IDX_GENARGS);
  if (!subs_count_atleast(genargs, 1)) {
    return 0;
  }

  const struct typ *t0 = node_toplevel(node)->generic->our_generic_functor_typ;

  size_t n = 0;
  FOREACH_SUB(ga, genargs) {
    if (ga->which == SETGENARG) {
      error e = typ_check_isa(mod, ga->as.SETGENARG.for_error,
                              ga->typ, typ_generic_arg_const(t0, n));
      EXCEPT(e);
      n += 1;
    }
  }

  return 0;
}

static STEP_NM(step_validate_genargs,
               STEP_NM_DEFS);
static error step_validate_genargs(struct module *mod, struct node *node,
                                   void *user, bool *stop) {
  DSTEP(mod, node);

  // We couldn't do this before -- we needed to wait until all genargs were
  // typed across the compilation unit.
  error e = validate_genarg_types(mod, node);
  EXCEPT(e);

  return 0;
}

static STEP_NM(step_type_lets,
               NM(LET));
static error step_type_lets(struct module *mod, struct node *node,
                            void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *par = parent(node);
  if ((node_is_at_top(node) || node_is_at_top(par))
      && subs_first(node)->which == DEFNAME) {
    error e = morningtypepass(mod, node);
    EXCEPT(e);
  }

  return 0;
}

static STEP_NM(step_type_deffields,
               NM(DEFCHOICE) | NM(DEFFIELD));
static error step_type_deffields(struct module *mod, struct node *node,
                                 void *user, bool *stop) {
  DSTEP(mod, node);

  error e = morningtypepass(mod, node);
  EXCEPT(e);

  return 0;
}

static STEP_NM(step_type_defchoices,
               NM(DEFTYPE) | NM(DEFCHOICE));
static error step_type_defchoices(struct module *mod, struct node *node,
                                  void *user, bool *stop) {
  DSTEP(mod, node);

  if (node->which == DEFTYPE) {
    switch (node->as.DEFTYPE.kind) {
    case DEFTYPE_ENUM:
    case DEFTYPE_UNION:
      break;
    default:
      return 0;
    }
  }

  set_typ(&node->as.DEFTYPE.tag_typ,
          typ_create_tentative(TBI_LITERALS_INTEGER));

  error e;
  FOREACH_SUB(ch, node) {
    if (ch->which != DEFCHOICE) {
      continue;
    }

    e = unify(mod, ch, node->as.DEFTYPE.tag_typ,
              subs_at(ch, IDX_CH_TAG_FIRST)->typ);
    EXCEPT(e);
    e = unify(mod, ch, node->as.DEFTYPE.tag_typ,
              subs_at(ch, IDX_CH_TAG_LAST)->typ);
    EXCEPT(e);

    ch->flags |= NODE_IS_DEFCHOICE;
  }

  if (typ_definition_const(node->as.DEFTYPE.tag_typ)->which == DEFINTF
      || typ_equal(node->as.DEFTYPE.tag_typ, TBI_LITERALS_INTEGER)) {
    e = unify(mod, node, node->as.DEFTYPE.tag_typ, TBI_U32);
    EXCEPT(e);
  }

  return 0;
}

static STEP_NM(step_type_deffuns,
               NM(DEFMETHOD) | NM(DEFFUN));
static error step_type_deffuns(struct module *mod, struct node *node,
                               void *user, bool *stop) {
  DSTEP(mod, node);

  error e = morningtypepass(mod, node);
  EXCEPT(e);

  return 0;
}

static STEP_NM(step_rewrite_def_return_through_ref,
               NM(DEFFUN) | NM(DEFMETHOD));
static error step_rewrite_def_return_through_ref(struct module *mod, struct node *node,
                                                 void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *retval = node_fun_retval(node);
  if (typ_isa(retval->typ, TBI_RETURN_BY_COPY)) {
    return 0;
  }

  if (retval->which == DEFARG) {
    return 0;
  }

  struct node *funargs = subs_at(node, IDX_FUNARGS);
  struct node *named = mk_node(mod, funargs, DEFARG);
  named->as.DEFARG.is_retval = true;
  struct node *name = mk_node(mod, named, IDENT);
  name->as.IDENT.name = ID_NRETVAL;
  node_subs_remove(funargs, named);
  node_subs_replace(funargs, retval, named);
  node_subs_append(named, retval);

  error e = lexical_retval(mod, node, named);
  EXCEPT(e);

  const struct node *except[] = { retval, NULL };
  e = catchup(mod, except, named, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);

  set_typ(&name->typ, TBI__NOT_TYPEABLE);
  set_typ(&named->typ, retval->typ);

  return 0;
}

static error passfwd0(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // scoping_deftypes
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_codeloc_for_generated);
    DOWN_STEP(step_export_pre_post_invariant);
    DOWN_STEP(step_lexical_scoping);
    ,
    UP_STEP(step_type_definitions);
    UP_STEP(step_complete_instantiation);
    UP_STEP(step_move_detached_members);
    );
  return 0;
}

static error passfwd1(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // imports
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_lexical_import);
    DOWN_STEP(step_add_builtin_members);
    DOWN_STEP(step_stop_funblock);
    ,
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd2(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_genargs
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_type_inference_genargs);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_type_aliases);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd3(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_isalist
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_type_create_update);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_type_inference_isalist);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd4(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_complete_create
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_validate_genargs);
    DOWN_STEP(step_type_update_quickisa);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    ,
    );
  return 0;
}

static error passfwd5(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_def_lets
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_type_lets);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd6(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_deffields
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_type_deffields);
    UP_STEP(step_type_defchoices);
    UP_STEP(step_add_builtin_members_enum_union);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd7(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_add_builtin_intf
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_autointf_enum_union);
    UP_STEP(step_autointf_detect_default_ctor_dtor);
    UP_STEP(step_autointf_infer_intfs);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

static error passfwd8(struct module *mod, struct node *root,
                      void *user, ssize_t shallow_last_up) {
  // type_deffuns
  PASS(
    DOWN_STEP(step_stop_submodules);
    DOWN_STEP(step_stop_marker_tbi);
    DOWN_STEP(step_stop_funblock);
    DOWN_STEP(step_push_top_state);
    ,
    UP_STEP(step_type_deffuns);
    UP_STEP(step_autointf_add_environment_builtins);
    UP_STEP(step_rewrite_def_return_through_ref);
    UP_STEP(step_pop_top_state);
    UP_STEP(step_complete_instantiation);
    );
  return 0;
}

a_pass passfwd[] = {
  passfwd0,
  passfwd1,
  passfwd2,
  passfwd3,
  passfwd4,
  passfwd5,
  passfwd6,
  passfwd7,
  passfwd8,
};
