#include "inference.h"

#include "passes.h"
#include "ssa.h"
#include "types.h"
#include "unify.h"
#include "scope.h"
#include "parser.h"
#include "instantiate.h"
#include "topdeps.h"
#include "passfwd.h"
#include "passbody.h"

static struct typ *create_tentative(struct module *mod, const struct node *for_error,
                                    struct typ *functor) {
  return instantiate_fully_implicit(mod, for_error, functor);
}

// Avoid the creation of a tentative typ for the interface if `t` already
// isa the intf.
static ERROR unify_intf(struct module *mod, const struct node *for_error,
                        struct typ *t, struct typ *intf) {
  if (typ_isa(t, intf)) {
    return 0;
  }

  return unify(mod, for_error, t, create_tentative(mod, for_error, intf));
}

static void mark_subs(struct module *mod, struct node *node, struct typ *mark,
                      struct node *first, struct node *last, size_t incr) {
  if (first == NULL) {
    return;
  }

  struct node *n = first;
  while (true) {
    n->typ = mark;

    for (size_t i = 0; i < incr; ++i) {
      if (n == last) {
        return;
      }

      n = next(n);
    }
  }
}

static void inherit(struct module *mod, struct node *node) {
  if (node->typ == TBI__NOT_TYPEABLE) {
    mark_subs(mod, node, node->typ,
              subs_first(node), subs_last(node), 1);
  }
}

STEP_NM(step_type_destruct_mark,
        NM(BIN) | NM(CALL) | NM(INIT) | NM(DEFALIAS) | NM(DEFNAME) |
        NM(DEFFUN) | NM(DEFMETHOD) | NM(DEFTYPE) | NM(DEFINTF) | NM(DEFINCOMPLETE) |
        NM(DEFFIELD) | NM(DEFARG) | NM(DEFGENARG) | NM(SETGENARG) |
        NM(DEFCHOICE) | NM(WITHIN) | NM(THROW));
error step_type_destruct_mark(struct module *mod, struct node *node,
                              void *user, bool *stop) {
  DSTEP(mod, node);
  if (node->which == MODULE) {
    return 0;
  }

  inherit(mod, node);

  struct typ *not_typeable = TBI__NOT_TYPEABLE;
  struct node *first = subs_first(node);
  struct node *last = subs_last(node);

  switch (node->which) {
  case BIN:
    if (OP_KIND(node->as.BIN.operator) == OP_BIN_ACC) {
      last->typ = not_typeable;

      const struct node *par = parent_const(node);
      if (NM(par->which) & (NM(DEFFUN) | NM(DEFMETHOD))
          && subs_first_const(par) == node) {
        first->typ = NULL;
      }
    }
    break;
  case CALL:
    if (first->typ == NULL) {
      // Otherwise, we are rewriting this expression and we should not touch
      // first.
      first->typ = TBI__CALL_FUNCTION_SLOT;
    }
    break;
  case INIT:
    if (!node->as.INIT.is_array && !node->as.INIT.is_defchoice_external_payload_constraint) {
      mark_subs(mod, node, TBI__NOT_TYPEABLE, first, last, 2);
    }
    break;
  case WITHIN:
    if (subs_count_atleast(node, 1)
        && first->which != WITHIN) {
      // So it will not resolve in type_inference_ident(), but through
      // type_inference_within().
      first->typ = not_typeable;
    }
    break;
  case DEFFUN:
  case DEFMETHOD:
    first->typ = not_typeable;
    break;
  case DEFALIAS:
  case DEFNAME:
  case DEFTYPE:
  case DEFINTF:
  case DEFINCOMPLETE:
  case DEFFIELD:
  case DEFARG:
  case DEFGENARG:
  case SETGENARG:
    first->typ = not_typeable;
    break;
  case DEFCHOICE:
    first->typ = not_typeable;
    break;
  case THROW:
    if (subs_count_atleast(node, 2)) {
      first->typ = not_typeable;
    }
    break;
  default:
    assert(false && "Unreached");
    break;
  }

  return 0;
}

STEP_NM(step_type_mutability_mark,
        NM(BIN) | NM(UN));
error step_type_mutability_mark(struct module *mod, struct node *node,
                                void *user, bool *stop) {
  DSTEP(mod, node);
  struct node *first = subs_first(node);
  struct typ *against = NULL;

  switch (node->which) {
  case BIN:
    switch (node->as.BIN.operator) {
    case TASSIGN:
    case TPLUS_ASSIGN:
    case TMINUS_ASSIGN:
    case TTIMES_ASSIGN:
    case TDIVIDE_ASSIGN:
    case TMODULO_ASSIGN:
    case TBWAND_ASSIGN:
    case TBWOR_ASSIGN:
    case TBWXOR_ASSIGN:
    case TRSHIFT_ASSIGN:
    case TOVLSHIFT_ASSIGN:
      first->typ = TBI__MUTABLE;
      break;
    default:
      break;
    }
    break;
  case UN:
    if (OP_KIND(node->as.UN.operator) != OP_UN_REFOF) {
      break;
    }

    // Below, we're interested in catching cases like: @#(self!p)

    switch (node->as.UN.operator) {
    case TREFWILDCARD:
    case TREFDOT:
    case TCREFDOT:
    case TCREFWILDCARD:
      // no-op
      break;
    case TREFBANG:
    case TCREFBANG:
      against = TBI__MUTABLE;
      break;
    case TREFSHARP:
    case TCREFSHARP:
      against = TBI__MERCURIAL;
      break;
    default:
      break;
    }
    break;
  default:
    assert(false && "Unreached");
    break;
  }

  if (against != NULL) {
    if (first->typ != NULL) {
      if (first->which == BIN && !(first->flags & NODE_IS_TYPE)) {
        error e = typ_check_deref_against_mark(mod, first, against,
                                               first->as.BIN.operator);
        EXCEPT(e);
      }
    } else {
      first->typ = against;
    }
  }

  return 0;
}

STEP_NM(step_type_gather_retval,
        NM(DEFFUN) | NM(DEFMETHOD));
error step_type_gather_retval(struct module *mod, struct node *node,
                              void *user, bool *stop) {
  DSTEP(mod, node);

  module_retval_set(mod, node_fun_retval(node));

  return 0;
}

// FIXME: This is O(depth * number_throw_except).
// Would be O(number_throw_except) if we remembered whether we're in the TRY
// or in one of the CATCH, when descending.
static ERROR check_in_try(struct module *mod, struct node *node, const char *which) {
  error e;

  struct try_state *st = module_excepts_get(mod);
  if (st == NULL) {
    goto fail;
  }

  const struct node *p = node;
  do {
    p = parent_const(p);
    if (p->which == CATCH) {
      goto fail;
    }
  } while (p->which != TRY);

  goto ok;

fail:
  e = mk_except(mod, node, "%s not in try block", which);
  THROW(e);

ok:
  return 0;
}

STEP_NM(step_type_gather_excepts,
        NM(TRY) | NM(THROW));
error step_type_gather_excepts(struct module *mod, struct node *node,
                               void *user, bool *stop) {
  DSTEP(mod, node);
  error e;
  switch (node->which) {
  case TRY:
    module_excepts_open_try(mod, node);
    break;
  case THROW:
    e = check_in_try(mod, node, "throw");
    EXCEPT(e);
    module_excepts_push(mod, node);
    break;
  default:
    assert(false && "Unreached");
    break;
  }
  return 0;
}

static ERROR optional(struct typ **result,
                      struct module *mod, struct node *for_error,
                      struct typ *typ) {
  if (typ_equal(typ, TBI_LITERALS_NIL)) {
    return 0;
  }
  error e = instantiate(result, mod, for_error, 0, TBI_OPTIONAL, &typ, 1, false);
  EXCEPT(e);

  topdeps_record(mod, *result);
  return 0;
}

static ERROR reference_functor(struct typ **result,
                               struct module *mod, struct node *for_error,
                               struct typ *functor, struct typ *typ) {
  error e = instantiate(result, mod, for_error, 0, functor, &typ, 1, false);
  EXCEPT(e);

  topdeps_record(mod, *result);
  return 0;
}

error reference(struct typ **result,
                struct module *mod, struct node *for_error,
                enum token_type op, struct typ *typ) {
  struct typ *f = mod->gctx->builtin_typs_for_refop[op];
  error e = reference_functor(result, mod, for_error, f, typ);
  EXCEPT(e);
  return 0;
}

static struct typ *counted_functor(struct typ *t) {
  struct typ *t0 = typ_is_generic_functor(t) ? t : typ_generic_functor(t);
  if (typ_isa(t0, TBI_ANY_COUNTED_REF)) {
    return t0;
  }

  if (typ_equal(t0, TBI_ANY_LOCAL_REF)) {
    return TBI_ANY_COUNTED_REF;
  } else if (typ_equal(t0, TBI_ANY_LOCAL_NREF)) {
    return TBI_ANY_COUNTED_NREF;
  } else if (typ_equal(t0, TBI_REF)) {
    return TBI_CREF;
  } else if (typ_equal(t0, TBI_MREF)) {
    return TBI_CMREF;
  } else if (typ_equal(t0, TBI_MMREF)) {
    return TBI_CMMREF;
  } else if (typ_equal(t0, TBI_NREF)) {
    return TBI_CNREF;
  } else if (typ_equal(t0, TBI_NMREF)) {
    return TBI_CNMREF;
  } else if (typ_equal(t0, TBI_NMMREF)) {
    return TBI_CNMMREF;
  } else {
    assert(false);
    return NULL;
  }
}

static struct typ *nullable_functor(struct typ *t) {
  struct typ *t0 = typ_is_generic_functor(t) ? t : typ_generic_functor(t);
  if (typ_isa(t0, TBI_ANY_NREF)) {
    return t0;
  }

  if (typ_equal(t0, TBI_ANY_REF)) {
    return TBI_ANY_NREF;
  } else if (typ_equal(t0, TBI_ANY_MREF)) {
    return TBI_ANY_NMREF;
  } else if (typ_equal(t0, TBI_ANY_LOCAL_REF)) {
    return TBI_ANY_LOCAL_NREF;
  } else if (typ_equal(t0, TBI_ANY_LOCAL_MREF)) {
    return TBI_ANY_LOCAL_NMREF;
  } else if (typ_equal(t0, TBI_REF)) {
    return TBI_NREF;
  } else if (typ_equal(t0, TBI_MREF)) {
    return TBI_NMREF;
  } else if (typ_equal(t0, TBI_MMREF)) {
    return TBI_NMMREF;
  } else if (typ_equal(t0, TBI_CREF)) {
    return TBI_CNREF;
  } else if (typ_equal(t0, TBI_CMREF)) {
    return TBI_CNMREF;
  } else if (typ_equal(t0, TBI_CMMREF)) {
    return TBI_CNMMREF;
  } else {
    assert(false);
    return NULL;
  }
}

static struct typ *nonnullable_functor(struct typ *t) {
  struct typ *t0 = typ_is_generic_functor(t) ? t : typ_generic_functor(t);
  if (!typ_isa(t0, TBI_ANY_NREF)) {
    return t0;
  }

  if (typ_equal(t0, TBI_ANY_NREF)) {
    return TBI_ANY_REF;
  } else if (typ_equal(t0, TBI_ANY_NMREF)) {
    return TBI_ANY_MREF;
  } else if (typ_equal(t0, TBI_ANY_LOCAL_NREF)) {
    return TBI_ANY_LOCAL_REF;
  } else if (typ_equal(t0, TBI_ANY_LOCAL_NMREF)) {
    return TBI_ANY_LOCAL_MREF;
  } else if (typ_equal(t0, TBI_NREF)) {
    return TBI_REF;
  } else if (typ_equal(t0, TBI_NMREF)) {
    return TBI_MREF;
  } else if (typ_equal(t0, TBI_NMMREF)) {
    return TBI_MMREF;
  } else if (typ_equal(t0, TBI_CNREF)) {
    return TBI_CREF;
  } else if (typ_equal(t0, TBI_CNMREF)) {
    return TBI_CMREF;
  } else if (typ_equal(t0, TBI_CNMMREF)) {
    return TBI_CMMREF;
  } else {
    assert(false);
    return NULL;
  }
}

struct wildcards {
  enum token_type ref;
  enum token_type nulref;
  enum token_type cref;
  enum token_type nulcref;
  enum token_type deref;
  enum token_type acc;
};

static void fill_wildcards(struct wildcards *w, struct typ *r0) {
  if (typ_equal(r0, TBI_REF)
      || typ_equal(r0, TBI_NREF)
      || typ_equal(r0, TBI_ANY_REF)
      || typ_equal(r0, TBI_ANY_NREF)
      || typ_equal(r0, TBI_ANY_LOCAL_REF)
      || typ_equal(r0, TBI_ANY_LOCAL_NREF)) {
    w->ref = TREFDOT;
    w->nulref = TNULREFDOT;
    w->cref = TCREFDOT;
    w->nulcref = TNULCREFDOT;
    w->deref = TDEREFDOT;
    w->acc = TDOT;
  } else if (typ_equal(r0, TBI_MREF) || typ_equal(r0, TBI_NMREF)) {
    w->ref = TREFBANG;
    w->nulref = TNULREFBANG;
    w->cref = TCREFBANG;
    w->nulcref = TNULCREFBANG;
    w->deref = TDEREFBANG;
    w->acc = TBANG;
  } else if (typ_equal(r0, TBI_MMREF)
             || typ_equal(r0, TBI_NMMREF)) {
    w->ref = TREFSHARP;
    w->nulref = TNULREFSHARP;
    w->cref = TCREFSHARP;
    w->nulcref = TNULCREFSHARP;
    w->deref = TDEREFSHARP;
    w->acc = TSHARP;
  } else {
    assert(false);
  }
}

static ERROR try_wildcard_op(struct typ **r0, enum token_type *rop,
                             struct module *mod, struct node *node) {
  error e;
  struct typ *w0 = NULL, *self0 = NULL;
  struct wildcards w = { 0 }, selfw = { 0 };

  struct node *top = mod->state->top_state->top;

  struct wildcards *ww = NULL;
  if (top->which == DEFFUN) {
    w0 = typ_definition_deffun_wildcard_functor(top->typ);
    if (w0 == NULL) {
      goto not_wildcard_fun;
    }

    fill_wildcards(&w, w0);

    *r0 = w0;
    ww = &w;
  } else if (top->which == DEFMETHOD) {
    w0 = typ_definition_defmethod_wildcard_functor(top->typ);
    if (w0 == NULL) {
      goto not_wildcard_fun;
    }
    self0 = typ_definition_defmethod_self_wildcard_functor(top->typ);

    fill_wildcards(&w, w0);
    fill_wildcards(&selfw, self0);

    if (node_ident(subs_first_const(node)) == ID_SELF) {
      *r0 = self0;
      ww = &selfw;
    } else {
      *r0 = w0;
      ww = &w;
    }
  } else {
    goto not_wildcard_fun;
  }

  bool reject_wildcard = false;
  if (false) {
not_wildcard_fun:
    reject_wildcard = true;
  }

  bool is_wildcard = true;
  switch (node->which) {
  case UN:
    switch (node->as.UN.operator) {
    case TREFWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->ref;
      }
      break;
    case TCREFWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->cref;
        *r0 = counted_functor(*r0);
      }
      break;
    case TNULREFWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->nulref;
        *r0 = nullable_functor(*r0);
      }
      break;
    case TNULCREFWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->nulcref;
        *r0 = counted_functor(nullable_functor(*r0));
      }
      break;
    case TDEREFWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->deref;
      }
      break;
    default:
      *r0 = mod->gctx->builtin_typs_for_refop[node->as.UN.operator];
      *rop = node->as.UN.operator;
      is_wildcard = false;
      break;
    }
    break;
  case BIN:
    switch (node->as.BIN.operator) {
    case TWILDCARD:
      if (!reject_wildcard) {
        *rop = ww->acc;
      }
      break;
    default:
      *r0 = mod->gctx->builtin_typs_for_refop[node->as.BIN.operator];
      *rop = node->as.BIN.operator;
      is_wildcard = false;
      break;
    }
    break;
  default:
    assert(false);
  }

  if (reject_wildcard && is_wildcard) {
    e = mk_except_type(mod, node, "cannot use wildcards outside"
                       " of a wildcard function or method");
    THROW(e);
  }

  return 0;
}

static ERROR wrap_arg_unary_op(struct node **r, struct module *mod,
                               enum token_type op) {
  error e;
  switch (op) {
  case TDEREFDOT:
  case TDEREFBANG:
  case TDEREFSHARP:
  case TDEREFWILDCARD:
  case T__DEOPT:
    if (typ_equal((*r)->typ, TBI_LITERALS_NIL)) {
      e = mk_except_type(mod, *r, "cannot convert nil literal to value");
      THROW(e);
    }
    break;
  default:
    break;
  }

  struct node *arg = *r;
  const bool is_named = arg->which == CALLNAMEDARG;
  struct node *real_arg = is_named ? subs_first(arg) : arg;

  struct node *before = prev(real_arg);
  struct node *par = parent(real_arg);
  node_subs_remove(par, real_arg);
  struct node *deref_arg = mk_node(mod, par, UN);
  deref_arg->as.UN.operator = op;
  node_subs_append(deref_arg, real_arg);
  node_subs_remove(par, deref_arg);
  node_subs_insert_after(par, before, deref_arg);

  if (is_named) {
    unset_typ(&arg->typ);
  }

  const struct node *except[] = { real_arg, NULL };
  e = catchup(mod, except,
              is_named ? arg : deref_arg,
              CATCHUP_BELOW_CURRENT);
  EXCEPT(e);

  if (!is_named) {
    // arg was changed
    *r = deref_arg;
  }
  return 0;
}

static ERROR check_terms_not_types(struct module *mod, struct node *node) {
  error e;
  int nth = 1;
  FOREACH_SUB_CONST(s, node) {
    if (s->flags & NODE_IS_TYPE) {
      e = mk_except_type(mod, s, "term %d cannot be a type", nth);
      THROW(e);
    }
    nth += 1;
  }
  return 0;
}

static struct node *follow_ssa(struct node *node) {
  struct node *expr = node;
  if (expr->which == IDENT) {
    struct node *def = expr->as.IDENT.def;
    if (def == NULL) {
      return NULL;
    }

    if (def->which == DEFNAME
        && def->as.DEFNAME.ssa_user == expr) {
      expr = subs_last(def);
    }
  }
  return expr;
}

static ERROR insert_automagic_de(struct node **r,
                                 struct module *mod,
                                 enum token_type op) {
  if (typ_equal((*r)->typ, TBI_LITERALS_NIL)) {
    error e = mk_except_type(mod, *r, "cannot convert nil literal to value");
    THROW(e);
  }

  struct node *node = *r;
  struct node *expr = follow_ssa(node);
  if (expr != NULL
      && expr->which == UN
      && expr->as.UN.operator == TREFDOT
      && expr->as.UN.is_explicit) {
    error e = mk_except_type(mod, expr, "explicit '*' operators are not"
                             " allowed for unqualified const references");
    THROW(e);
  }

  struct node *last_block = NULL;
  struct node *target = node;
  while (target->which == BLOCK) {
    last_block = target;
    unset_typ(&last_block->typ);

    target = subs_last(target);
  }

  struct node *par = parent(target);
  struct node *deref = mk_node(mod, par, UN);
  deref->codeloc = target->codeloc;
  deref->as.UN.operator = op;

  node_subs_remove(par, deref);
  node_subs_replace(par, target, deref);
  node_subs_append(deref, target);

  const struct node *except[] = { target, NULL };
  error e = catchup(mod, except, deref, CATCHUP_BELOW_CURRENT);
  assert(!e);

  if (last_block != NULL) {
    struct node *n = last_block;
    while (true) {
      assert(n->which == BLOCK);
      set_typ(&n->typ, subs_last(n)->typ);

      if (n == node) {
        break;
      }
      n = parent(n);
    }
  }

  *r = deref;
  return 0;
}

// May modifies node. Get the new one after calling this (same location in
// the tree).
error try_insert_automagic_de(struct module *mod, struct node *node) {
  if (typ_is_optional(node->typ)) {
    error e = insert_automagic_de(&node, mod, T__DEOPT);
    EXCEPT(e);
  }
  if (typ_is_reference(node->typ)) {
    error e = insert_automagic_de(&node, mod, TDEREFDOT);
    EXCEPT(e);
  }
  if (typ_is_optional(node->typ)) {
    error e = insert_automagic_de(&node, mod, T__DEOPT);
    EXCEPT(e);
  }
  return 0;
}

static ERROR nullable_op(enum token_type *r,
                         struct module *mod, const struct node *for_error,
                         struct typ *t) {
  if (typ_isa(t, TBI_ANY_NREF)) {
    *r = 0;
    return 0;
  }

  struct typ *t0 = typ_generic_functor(t);
  if (t0 == NULL || !typ_is_reference(t)) {
    error e = mk_except_type(mod, for_error, "Nullable expects a reference, not '%s'",
                             pptyp(mod, t));
    THROW(e);
  }

  if (typ_equal(t0, TBI_CMMREF)) {
    *r = TNULCREFSHARP;
  } else if (typ_equal(t0, TBI_CMREF)) {
    *r = TNULCREFBANG;
  } else if (typ_equal(t0, TBI_CREF)) {
    *r = TNULCREFDOT;
  } else if (typ_equal(t0, TBI_MMREF)) {
    *r = TNULREFSHARP;
  } else if (typ_isa(t0, TBI_ANY_MREF)) {
    *r = TNULREFBANG;
  } else if (typ_isa(t0, TBI_ANY_REF)) {
    *r = TNULREFDOT;
  } else {
    assert(false);
  }

  return 0;
}

static ERROR nonnullable_op(enum token_type *r,
                            struct module *mod, const struct node *for_error,
                            struct typ *t) {
  if (!typ_isa(t, TBI_ANY_NREF)) {
    *r = 0;
    return 0;
  }

  struct typ *t0 = typ_generic_functor(t);
  if (t0 == NULL || !typ_is_reference(t)) {
    error e = mk_except_type(mod, for_error, "nonnullable expects a reference or an optional value, not '%s'",
                             pptyp(mod, t));
    THROW(e);
  }

  if (typ_equal(t0, TBI_CNMMREF)) {
    *r = TCREFSHARP;
  } else if (typ_equal(t0, TBI_CNMREF)) {
    *r = TCREFBANG;
  } else if (typ_isa(t0, TBI_ANY_COUNTED_NREF)) {
    *r = TCREFDOT;
  } else if (typ_equal(t0, TBI_NMMREF)) {
    *r = TREFSHARP;
  } else if (typ_isa(t0, TBI_ANY_NMREF)) {
    *r = TREFBANG;
  } else if (typ_isa(t0, TBI_ANY_NREF)) {
    *r = TREFDOT;
  } else {
    assert(false);
  }

  return 0;
}

static void rewrite_un_qmark(struct module *mod, struct node *node) {
  struct node *sub = subs_first(node);

  if (node->as.UN.operator == TPREQMARK) {
    node_set_which(node, INIT);
    node->as.INIT.is_optional = true;
    node_subs_remove(node, sub);

    GSTART();
    G0(x, node, IDENT,
       x->as.IDENT.name = ID_X);
    node_subs_append(node, sub);
    G0(nonnill, node, IDENT,
       nonnill->as.IDENT.name = ID_NONNIL);
    G0(t, node, BOOL,
       t->as.BOOL.value = true);
  } else if (node->as.UN.operator == T__DEOPT) {
    node_set_which(node, BIN);
    node->as.BIN.is_generated = true;
    if (node->typ == TBI__MERCURIAL) {
      node->as.BIN.operator = TSHARP;
    } else if (node->typ == TBI__MUTABLE) {
      node->as.BIN.operator = TBANG;
    } else {
      node->as.BIN.operator = TDOT;
    }
    GSTART();
    G0(x, node, IDENT,
       x->as.IDENT.name = ID_X);
  } else {
    assert(false);
  }

  const struct node *except[] = { sub, NULL };
  error e = catchup(mod, except, node, CATCHUP_REWRITING_CURRENT);
  assert(!e);
}

static void try_insert_nonnullable_void(struct module *mod, struct node *node,
                                        struct node *term) {
  if (!typ_equal(term->typ, TBI_VOID)) {
    return;
  }

  struct node *statement = find_current_statement(node);
  struct node *statement_parent = parent(statement);

  GSTART();
  G0(x, node, IDENT,
     x->as.IDENT.name = idents_add_string(mod->gctx, "Nonnil_void", 11));

  node_subs_remove(node, x);
  node_subs_replace(node, term, x);
  node_subs_insert_before(statement_parent, statement, term);

  error e = catchup(mod, NULL, x, CATCHUP_BELOW_CURRENT);
  assert(!e);
}

static ERROR type_inference_un(struct module *mod, struct node *node) {
  assert(node->which == UN);
  error e;

  enum token_type rop = 0;
  struct typ *rfunctor = NULL;
  e = try_wildcard_op(&rfunctor, &rop, mod, node);
  EXCEPT(e);

  struct node *term = subs_first(node);
  struct typ *i = NULL;

rewrote_op:
  if ((rop == TPREQMARK && (!(term->flags & NODE_IS_TYPE)
                            && !typ_equal(term->typ, TBI_VOID)))
      || rop == T__DEOPT) {
    rewrite_un_qmark(mod, node);
    return 0;
  }

  switch (OP_KIND(rop)) {
  case OP_UN_PRIMITIVES:
    switch (rop) {
    case T__NULLABLE:
      {
        if (!typ_is_reference(term->typ)) {
          rop = TPREQMARK;
          node->as.UN.operator = rop;
          goto rewrote_op;
        }

        enum token_type nop = 0;
        e = nullable_op(&nop, mod, node, term->typ);
        EXCEPT(e);
        if (nop == 0) {
          set_typ(&node->typ, term->typ);
        } else {
          e = reference(&i, mod, node, nop, typ_generic_arg(term->typ, 0));
          EXCEPT(e);
          set_typ(&node->typ, i);
          node->flags |= term->flags & NODE__TRANSITIVE;
        }
      }
      break;
    case T__NONNULLABLE:
      {
        if (typ_is_optional(term->typ)) {
          rop = T__DEOPT;
          node->as.UN.operator = rop;
          goto rewrote_op;
        }

        enum token_type nop = 0;
        e = nonnullable_op(&nop, mod, node, term->typ);
        EXCEPT(e);
        if (nop == 0) {
          set_typ(&node->typ, term->typ);
        } else {
          e = reference(&i, mod, node, nop, typ_generic_arg(term->typ, 0));
          EXCEPT(e);
          set_typ(&node->typ, i);
          node->flags |= term->flags & NODE__TRANSITIVE;
        }
      }
      break;
    default:
      assert(false);
    }
    return 0;
  case OP_UN_SLICE:
    if (!(term->flags & NODE_IS_TYPE)) {
      e = mk_except_type(mod, node, "slice specifier must be applied to a type");
      THROW(e);
    }
    // fallthrough
  case OP_UN_REFOF:
    // FIXME: it's not OK to take a mutable reference of:
    //   fun foo p:@t = void
    //     let mut = @!(p.)
    if ((rop == TCREFDOT || rop == TCREFBANG || rop == TCREFSHARP || rop == TCREFWILDCARD)
        && !(term->flags & NODE_IS_TYPE)) {
      e = mk_except_type(mod, node,
                         "counted reference specifiers (@ @! @# @$)"
                         " can only be applied to types, not values (hint: see Borrow)");
      THROW(e);
    }

    e = reference_functor(&i, mod, node, rfunctor, term->typ);
    EXCEPT(e);
    set_typ(&node->typ, i);
    node->flags |= term->flags & NODE__TRANSITIVE;
    return 0;
  case OP_UN_DEREF:
    e = typ_check_can_deref(mod, term, term->typ, rop);
    EXCEPT(e);
    e = typ_check_deref_against_mark(mod, node, node->typ, rop);
    EXCEPT(e);
    set_typ(&node->typ, typ_generic_arg(term->typ, 0));
    node->flags |= term->flags & NODE__TRANSITIVE;
    return 0;
  case OP_UN_OPT:
    switch (rop) {
    case TPOSTQMARK:
      if (!typ_is_optional(term->typ) && !typ_is_nullable_reference(term->typ)) {
        e = mk_except_type(mod, term, "postfix '?' must be used on reference"
                           " or optional type, not '%s'", pptyp(mod, term->typ));
        THROW(e);
      }
      set_typ(&node->typ, TBI_BOOL);
      break;
    case TPREQMARK:
      try_insert_nonnullable_void(mod, node, term);
      term = subs_first(node); // may have been just modified

      e = optional(&i, mod, node, term->typ);
      EXCEPT(e);
      set_typ(&node->typ, i);
      node->flags |= term->flags & NODE__TRANSITIVE;
      break;
    case T__DEOPT_DEREFDOT:
      if (typ_is_reference(term->typ)) {
        rop = TDEREFDOT;
      } else {
        rop = T__DEOPT;
      }
      node->as.UN.operator = rop;
      goto rewrote_op;
    default:
      assert(false);
    }
    return 0;
  case OP_UN_BOOL:
  case OP_UN_ADDARITH:
  case OP_UN_BW:
    break;
  default:
    assert(false);
  }

  e = check_terms_not_types(mod, node);
  EXCEPT(e);

  e = try_insert_automagic_de(mod, term);
  EXCEPT(e);
  // may have been modified, get the new one
  term = subs_first(node);

  switch (OP_KIND(rop)) {
  case OP_UN_BOOL:
    set_typ(&node->typ, TBI_BOOL);
    e = unify(mod, node, node->typ, term->typ);
    EXCEPT(e);
    break;
  case OP_UN_ADDARITH:
    set_typ(&node->typ, create_tentative(mod, node, TBI_ADDITIVE_ARITHMETIC));
    e = unify(mod, node, node->typ, term->typ);
    EXCEPT(e);
    break;
  case OP_UN_OVARITH:
    set_typ(&node->typ, create_tentative(mod, node, TBI_OVERFLOW_ARITHMETIC));
    e = unify(mod, node, node->typ, term->typ);
    EXCEPT(e);
    break;
  case OP_UN_BW:
    set_typ(&node->typ, create_tentative(mod, node, TBI_HAS_BITWISE_OPERATORS));
    e = unify(mod, node, node->typ, term->typ);
    EXCEPT(e);
    break;
  default:
    assert(false);
  }

  return 0;
}

static ERROR unfold_op_assign(struct module *mod, struct node *node) {
  assert(node->which == BIN);
  const enum token_type op = node->as.BIN.operator;
  assert(OP_IS_ASSIGN(op));

  enum token_type nop;
  switch (op) {
  case TPLUS_ASSIGN: nop = TPLUS; break;
  case TMINUS_ASSIGN: nop = TMINUS; break;
  case TTIMES_ASSIGN: nop = TTIMES; break;
  case TDIVIDE_ASSIGN: nop = TDIVIDE; break;
  case TMODULO_ASSIGN: nop = TMODULO; break;
  case TOVPLUS_ASSIGN: nop = TOVPLUS; break;
  case TOVMINUS_ASSIGN: nop = TOVMINUS; break;
  case TOVTIMES_ASSIGN: nop = TOVTIMES; break;
  case TOVDIVIDE_ASSIGN: nop = TOVDIVIDE; break;
  case TOVMODULO_ASSIGN: nop = TOVMODULO; break;
  case TBWAND_ASSIGN: nop = TBWAND; break;
  case TBWOR_ASSIGN: nop = TBWOR; break;
  case TBWXOR_ASSIGN: nop = TBWXOR; break;
  case TRSHIFT_ASSIGN: nop = TRSHIFT; break;
  case TOVLSHIFT_ASSIGN: nop = TOVLSHIFT; break;
  default: assert(false);
  }

  node->as.BIN.operator = TASSIGN;

  struct node *src = subs_first(node);
  struct node *arg = subs_last(node);
  node_subs_remove(node, src);
  node_subs_remove(node, arg);

  struct node *dst = node_new_subnode(mod, node);
  node_deepcopy(mod, dst, src);

  struct node *expr = mk_node(mod, node, BIN);
  expr->as.BIN.operator = nop;
  node_subs_append(expr, src);
  node_subs_append(expr, arg);

  const struct node *except[] = { src, arg, NULL };
  error e = catchup(mod, except, node, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);
  return 0;
}


static ERROR type_inference_bin_sym(struct module *mod, struct node *node) {
again:
  assert(node->which == BIN);
  enum token_type operator = node->as.BIN.operator;

  struct node *left = subs_first(node);
  struct node *right = subs_last(node);
  error e;

  if (OP_KIND(operator) != OP_BIN_SYM_PTR) {
    if (operator == TEQMATCH || operator == TNEMATCH) {
      // noop
    } else if (!OP_IS_ASSIGN(operator)) {
      e = try_insert_automagic_de(mod, left);
      EXCEPT(e);
      e = try_insert_automagic_de(mod, right);
      EXCEPT(e);

      left = subs_first(node);
      right = subs_last(node);
    } else if (operator != TASSIGN) {
      e = try_insert_automagic_de(mod, right);
      EXCEPT(e);

      right = subs_last(node);
    }
  }

  if (operator == TASSIGN) {
    if (typ_equal(right->typ, TBI_VOID)) {
      if (left->which == IDENT
          && left->as.IDENT.def != NULL
          && left->as.IDENT.def->which == DEFNAME
          && left->as.IDENT.def->as.DEFNAME.ssa_user != NULL) {
        // noop
      } else {
        e = mk_except(mod, node, "cannot assign an expression of type 'void'");
        THROW(e);
      }
    }

    const bool lisotherwise = node_ident(left) == ID_OTHERWISE;
    const bool lisnil = typ_equal(left->typ, TBI_LITERALS_NIL);
    const bool lisdefinc = typ_definition_which(left->typ) == DEFINCOMPLETE;
    const bool lisref = typ_is_reference(left->typ);
    const bool lisopt = typ_is_optional(left->typ);
    const bool risnil = typ_equal(right->typ, TBI_LITERALS_NIL);
    const bool risref = typ_is_reference(right->typ);
    const bool risopt = typ_is_optional(right->typ);
    if (!lisotherwise && !lisdefinc && !lisref && !lisopt && !risnil && (risref || risopt)) {
      e = try_insert_automagic_de(mod, right);
      EXCEPT(e);
      right = subs_last(node);
    }
    if (!lisotherwise && !lisnil && lisopt && !risopt) {
      e = wrap_arg_unary_op(&right, mod, TPREQMARK);
      EXCEPT(e);
      right = subs_last(node);
    }

    e = unify_refcompat(mod, node, left->typ, right->typ);
    EXCEPT(e);

    left->flags |= right->flags & NODE__ASSIGN_TRANSITIVE;
  } else if (operator == TEQMATCH || operator == TNEMATCH) {
    // noop
  } else if (OP_KIND(operator) == OP_BIN_SYM_PTR) {
    // noop, see below
  } else {
    e = unify(mod, node, left->typ, right->typ);
    EXCEPT(e);
  }

  struct typ *arith = NULL, *arith_assign = NULL;
  switch (OP_KIND(operator)) {
  case OP_BIN_SYM_ADDARITH:
    arith = TBI_ADDITIVE_ARITHMETIC;
    arith_assign = TBI_ADDITIVE_ARITHMETIC_ASSIGN;
    break;
  case OP_BIN_SYM_ARITH:
    arith = TBI_ARITHMETIC;
    arith_assign = TBI_ARITHMETIC_ASSIGN;
    break;
  case OP_BIN_SYM_OVARITH:
    arith = TBI_OVERFLOW_ARITHMETIC;
    arith_assign = TBI_OVERFLOW_ARITHMETIC;
    break;
  case OP_BIN_SYM_INTARITH:
    arith = TBI_INTEGER_ARITHMETIC;
    arith_assign = TBI_INTEGER_ARITHMETIC;
    break;

  case OP_BIN_SYM_BOOL:
    set_typ(&node->typ, left->typ);
    break;
  case OP_BIN_SYM_BW:
    switch (operator) {
    case TBWAND_ASSIGN:
    case TBWOR_ASSIGN:
    case TBWXOR_ASSIGN:
      e = unify_intf(mod, node, left->typ, TBI_HAS_BITWISE_OPERATORS);
      EXCEPT(e);

      set_typ(&node->typ, TBI_VOID);
      left->flags |= right->flags & NODE__TRANSITIVE;
      break;
    default:
      set_typ(&node->typ, create_tentative(mod, node, TBI_HAS_BITWISE_OPERATORS));
      e = unify(mod, node, node->typ, left->typ);
      EXCEPT(e);
      break;
    }
    break;
  case OP_BIN_SYM_PTR:
    e = unify_intf(mod, node, left->typ, TBI_ANY_ANY_REF);
    EXCEPT(e);
    e = unify_intf(mod, node, right->typ, TBI_ANY_ANY_REF);
    EXCEPT(e);
    if (typ_generic_arity(left->typ) > 0 && typ_generic_arity(right->typ) > 0) {
      // The kind of reference doesn't matter.
      // dyn should be Dyncast to a concrete type first. (At least, for now.)
      e = unify(mod, node, typ_generic_arg(left->typ, 0), typ_generic_arg(right->typ, 0));
      EXCEPT(e);
    } else {
      e = unify(mod, node, left->typ, right->typ);
      EXCEPT(e);
    }
    set_typ(&node->typ, TBI_BOOL);
    break;
  case OP_BIN_SYM:
    switch (operator) {
    case TLE:
    case TLT:
    case TGT:
    case TGE:
    case TEQ:
    case TNE:
      if (operator == TEQ || operator == TNE) {
        e = unify_intf(mod, left, left->typ, TBI_HAS_EQUALITY);
        EXCEPT(e);
      } else {
        e = unify_intf(mod, left, left->typ, TBI_PARTIALLY_ORDERED);
        EXCEPT(e);
      }

      set_typ(&node->typ, TBI_BOOL);
      break;
    case TEQMATCH:
    case TNEMATCH:
      {
        struct typ *texpr = left->typ;
        if (typ_is_reference(texpr)) {
          texpr = typ_generic_arg(texpr, 0);
        }

        if (operator == TEQMATCH) {
          node->as.BIN.operator = TEQ;
        } else if (operator == TNEMATCH) {
          node->as.BIN.operator = TNE;
        }

        if (operator == TEQMATCH && node->as.BIN.is_generated
            && (typ_definition_which(texpr) == DEFTYPE
                && typ_definition_deftype_kind(texpr) == DEFTYPE_STRUCT)) {
          // Introduced by LIR from a MATCH.
          goto again;
        }

        if (operator == TEQMATCH
            && (typ_definition_which(texpr) == DEFTYPE
                && typ_definition_deftype_kind(texpr) == DEFTYPE_ENUM)) {
          goto again;
        }

        if (typ_definition_which(texpr) != DEFTYPE
            || typ_definition_deftype_kind(texpr) != DEFTYPE_UNION) {
          e = mk_except_type(mod, left, "match operator must be used on a union,"
                             " not on type '%s'", pptyp(mod, texpr));
          THROW(e);
        }

        if (typ_member(texpr, node_ident(right)) == NULL) {
          e = mk_except_type(mod, right, "unknown ident '%s' in match"
                             " operator pattern",
                             idents_value(mod->gctx, node_ident(right)));
          THROW(e);
        }

        e = unify(mod, right, right->typ, texpr);
        EXCEPT(e);

        GSTART();
        G0(acc, node, BIN,
           acc->as.BIN.operator = TDOT;
           node_subs_remove(node, acc);
           node_subs_replace(node, left, acc);
           node_subs_append(acc, left);
           G(tag, IDENT,
             tag->as.IDENT.name = ID_TAG));

        const struct node *except[] = { left, NULL };
        e = catchup(mod, except, acc, CATCHUP_BELOW_CURRENT);
        EXCEPT(e);
        left = acc;

        struct tit *ttag = typ_definition_one_member(texpr, node_ident(right));
        const struct node *tag_expr = tit_defchoice_tag_expr(ttag);
        tit_next(ttag);
        node_deepcopy(mod, right, tag_expr);
        e = catchup(mod, NULL, right, CATCHUP_BELOW_CURRENT);
        EXCEPT(e);

        e = unify(mod, right, left->typ, right->typ);
        EXCEPT(e);
        set_typ(&node->typ, TBI_BOOL);
      }
      break;
    default:
      set_typ(&node->typ, TBI_VOID);
      break;
    }
    break;
  case OP_BIN:
    set_typ(&node->typ, TBI_VOID);
    break;
  default:
    assert(false);
    break;
  }

  if (arith != NULL) {
    assert(arith_assign != NULL);
    if (OP_IS_ASSIGN(operator)) {
      if (typ_isa(left->typ, arith_assign)) {
        set_typ(&node->typ, TBI_VOID);
        left->flags |= right->flags & NODE__TRANSITIVE;
      } else {
        e = unfold_op_assign(mod, node);
        EXCEPT(e);
      }
    } else {
      set_typ(&node->typ, create_tentative(mod, node, arith));
      e = unify(mod, node, node->typ, left->typ);
      EXCEPT(e);
    }
  }

  return 0;
}

static char *quote_code(const char *data, size_t start) {
  int len = 0;
  for (size_t n = start; true; ++n, ++len) {
    if (data[n] == '\n') {
      goto done;
    }
  }

done:;
  char *r = calloc(2 + len + 1, sizeof(char));
  sprintf(r, "\"%.*s\"", len, data + start);
  return r;
}

EXAMPLE(quote_code) {
  const char *code = "abcd\nefgh";
  assert(strcmp("\"abcd\"", quote_code(code, 0)) == 0);
  assert(strcmp("\"efgh\"", quote_code(code, 5)) == 0);
  assert(strcmp("\"d\"", quote_code(code, 3)) == 0);
}

static void try_filling_codeloc(struct module *mod, struct node *named,
                                struct node *node) {
  if (node_ident(named) != ID_NCODELOC) {
    return;
  }

  node_subs_remove(named, subs_first(named));
  GSTART();
  G0(tc, named, TYPECONSTRAINT,
    G(init, INIT,
      init->flags |= NODE_IS_LOCAL_STATIC_CONSTANT;
      G_IDENT(filen, "File");
      G(file, STRING,
        file->flags |= NODE_IS_LOCAL_STATIC_CONSTANT);
      G_IDENT(linen, "Line");
      G(line, NUMBER,
        line->flags |= NODE_IS_LOCAL_STATIC_CONSTANT);
      G_IDENT(funn, "Function");
      G(fun, STRING,
        fun->flags |= NODE_IS_LOCAL_STATIC_CONSTANT);
      G_IDENT(exprn, "Expr");
      G(expr, STRING,
        expr->flags |= NODE_IS_LOCAL_STATIC_CONSTANT));
    G(t, DIRECTDEF,
      set_typ(&t->as.DIRECTDEF.typ, TBI_CODELOC)));

  const char *fn = module_component_filename_at(mod, node->codeloc.pos);
  file->as.STRING.value = fn;

  char *vl = calloc(16, sizeof(char));
  snprintf(vl, 16, "%d", node->codeloc.line);
  line->as.NUMBER.value = vl;

  fun->as.STRING.value = "";
  if (mod->state->top_state != NULL) {
    fun->as.STRING.value = idents_value(mod->gctx, node_ident(mod->state->top_state->top));
  }

  expr->as.STRING.value = quote_code(mod->parser.data, node->codeloc.pos);
}

static void insert_missing_optional_arg(struct module *mod, struct node *node,
                                        struct node *after_this, ident name) {
  assert(name != idents_add_string(mod->gctx, "v", 1));
  GSTART();
  G0(named, node, CALLNAMEDARG,
     named->as.CALLNAMEDARG.name = name;
     G(nil, NIL));
  node_subs_remove(node, named);
  node_subs_insert_after(node, after_this, named);

  try_filling_codeloc(mod, named, node);

  error e = catchup(mod, NULL, named, CATCHUP_BELOW_CURRENT);
  assert(!e);
}

static ERROR fill_in_optional_args(struct module *mod, struct node *node,
                                   const struct typ *tfun) {
  const size_t tmin = typ_function_min_arity(tfun);
  const size_t tmax = typ_function_max_arity(tfun);

  if (tmin == tmax) {
    return 0;
  }

  error e;
  struct node *arg = next(subs_first(node));
  ssize_t n = 0, code_pos = 0;
  for (; n < tmin; ++n, ++code_pos) {
    if (arg == NULL) {
      const struct node *dfun = typ_definition_ignore_any_overlay_const(tfun);
      const struct node *darg = subs_at_const(subs_at_const(dfun, IDX_FUNARGS), n);
      e = mk_except(mod, node, "missing positional argument '%s' at position %zd",
                    idents_value(mod->gctx, node_ident(darg)), code_pos);
      THROW(e);
    } else if (arg->which == CALLNAMEDARG && !arg->as.CALLNAMEDARG.is_slice_vararg) {
      if (node_ident(arg) != typ_function_arg_ident(tfun, n)) {
        e = mk_except(mod, arg, "named argument '%s' has bad name"
                      " or appears out of order at position %zd",
                      idents_value(mod->gctx, node_ident(arg)), code_pos);
        THROW(e);
      }
    }

    arg = next(arg);
  }

  const ssize_t first_vararg = typ_function_first_vararg(tfun);
  for (n = tmin; n < tmax && (first_vararg == - 1 || n < first_vararg); ++n) {
    const ident targ_name = typ_function_arg_ident(tfun, n);

    if (arg == NULL) {
      insert_missing_optional_arg(mod, node, subs_last(node), targ_name);

    } else if (arg->which != CALLNAMEDARG || arg->as.CALLNAMEDARG.is_slice_vararg) {
      // Assume this is the first vararg

      if (first_vararg == -1) {
        e = mk_except(mod, arg, "excessive positional argument"
                      " or optional argument lacks a name at position %zd", code_pos);
        THROW(e);
      }

      insert_missing_optional_arg(mod, node, prev(arg), targ_name);

    } else if (arg->which == CALLNAMEDARG && !arg->as.CALLNAMEDARG.is_slice_vararg) {
      const ident name = node_ident(arg);

      while (typ_function_arg_ident(tfun, n) != name) {
        insert_missing_optional_arg(mod, node, prev(arg),
                                    typ_function_arg_ident(tfun, n));

        n += 1;
        if ((first_vararg != -1 && n >= first_vararg) || n == tmax) {
          e = mk_except(mod, arg, "named argument '%s' has bad name"
                        " or appears out of order at position %zd",
                        idents_value(mod->gctx, name), code_pos);
          THROW(e);
        }
      }

      arg = next(arg);
      code_pos += 1;
    }
  }

  assert(arg == NULL || first_vararg >= 0);
  while (arg != NULL) {
    if (arg->which == CALLNAMEDARG && !arg->as.CALLNAMEDARG.is_slice_vararg) {
      const ident name = node_ident(arg);
      e = mk_except(mod, arg, "excess named argument '%s'"
                    " or appears out of order at position %zd",
                    idents_value(mod->gctx, name), code_pos);
      THROW(e);
    } else if (arg->which == CALLNAMEDARG && arg->as.CALLNAMEDARG.is_slice_vararg) {
      if (next(arg) != NULL) {
        e = mk_except(mod, next(arg), "excess argument appearing after a"
                      " slice or Vararg is passed as vararg,"
                      " at position %zd", code_pos);
        THROW(e);
      }
      break;
    }

    arg = next(arg);
    code_pos += 1;
  }

  return 0;
}

static ERROR rewrite_unary_call(struct module *mod, struct node *node, struct typ *tfun) {
  struct node *fun = node_new_subnode(mod, node);
  node_subs_remove(node, fun);
  node_move_content(fun, node);
  node_subs_append(node, fun);
  node_set_which(node, CALL);
  set_typ(&fun->typ, tfun);

  topdeps_record(mod, tfun);

  const struct node *except[] = { fun, NULL };
  error e = catchup(mod, except, node, CATCHUP_REWRITING_CURRENT);
  EXCEPT(e);

  return 0;
}

static ERROR rewrite_implicit_opt_acc_op(struct module *mod, struct node *node) {
  struct node *par = subs_first(node);
  node_subs_remove(node, par);
  GSTART();
  G0(nonnillable, node, UN,
     nonnillable->as.UN.operator = T__NONNULLABLE;
     node_subs_append(nonnillable, par));

  node_subs_remove(node, nonnillable);
  node_subs_insert_before(node, subs_first(node), nonnillable);

  const struct node *except[] = { par, NULL };
  error e = catchup(mod, except, nonnillable, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);
  return 0;
}

static ERROR type_inference_bin_accessor(struct module *mod, struct node *node) {
  error e;
  if (!node->as.BIN.is_generated && typ_is_optional(subs_first(node)->typ)) {
    e = rewrite_implicit_opt_acc_op(mod, node);
    EXCEPT(e);
  }

  const struct typ *mark = node->typ;

  enum token_type rop = 0;
  struct typ *rfunctor = NULL;
  e = try_wildcard_op(&rfunctor, &rop, mod, node);
  EXCEPT(e);
  node->as.BIN.operator = rop;

  bool container_is_tentative = false;
  struct tit *field = typ_resolve_accessor__has_effect(&e, &container_is_tentative,
                                                       mod, node);
  // e handled below.

  if (container_is_tentative && e == EINVAL) {
    assert(field == NULL);
    struct node *left = subs_first(node);
    struct node *name = subs_last(node);

    struct node *dinc = defincomplete_create(mod, node);
    defincomplete_add_field(mod, node, dinc, node_ident(name),
                            create_tentative(mod, node, TBI_ANY));
    e = defincomplete_catchup(mod, dinc);
    EXCEPT(e);

    e = unify(mod, node, left->typ, dinc->typ);
    EXCEPT(e);

    field = typ_definition_one_member(dinc->typ, node_ident(name));
  } else {
    EXCEPT(e);
  }

  struct typ *tfield = tit_typ(field);

  if (typ_is_function(tfield) && mark != TBI__CALL_FUNCTION_SLOT) {
    const bool is_method = typ_definition_which(tfield) == DEFMETHOD;
    if (typ_function_min_arity(tfield) != (is_method ? 1 : 0)) {
      e = mk_except_call_args_count(mod, node, tfield, is_method, 0);
      THROW(e);
    }

    e = rewrite_unary_call(mod, node, tfield);
    EXCEPT(e);
  } else {
    set_typ(&node->typ, tfield);
    node->flags = tit_node_flags(field);

    if (tit_which(field) == DEFNAME) {
      topdeps_record_global(mod, tit_node_ignore_any_overlay(field));
    }
  }

  if (!(node->flags & NODE_IS_TYPE)
      // Only used for globalenv underlying variable, user code
      // shouldn't be able to modify global constants.
      // FIXME(e): enforce LET const.
      && !(subs_first(node)->flags & NODE_IS_TYPE)) {
      if (!(subs_first(node)->flags & NODE_IS_DEFCHOICE)) {
      e = typ_check_deref_against_mark(mod, node, mark, rop);
      EXCEPT(e);
    }
  }

  tit_next(field);
  return 0;
}

static ERROR type_inference_bin_rhs_unsigned(struct module *mod, struct node *node) {
  error e;
  struct node *left = subs_first(node);
  struct node *right = subs_last(node);
  e = try_insert_automagic_de(mod, left);
  EXCEPT(e);
  e = try_insert_automagic_de(mod, right);
  EXCEPT(e);
  left = subs_first(node);
  right = subs_last(node);

  e = unify(mod, right, right->typ, TBI_UINT);
  EXCEPT(e);

  switch (node->as.BIN.operator) {
  case TRSHIFT:
    set_typ(&node->typ, create_tentative(mod, node, TBI_INTEGER_ARITHMETIC));
    e = unify(mod, node, left->typ, node->typ);
    EXCEPT(e);
    break;
  case TOVLSHIFT:
    set_typ(&node->typ, create_tentative(mod, node, TBI_OVERFLOW_ARITHMETIC));
    e = unify(mod, node, left->typ, node->typ);
    EXCEPT(e);
    break;
  case TRSHIFT_ASSIGN:
    e = unify_intf(mod, node, left->typ, TBI_INTEGER_ARITHMETIC);
    EXCEPT(e);
    set_typ(&node->typ, TBI_VOID);
    break;
  case TOVLSHIFT_ASSIGN:
    e = unify_intf(mod, node, left->typ, TBI_OVERFLOW_ARITHMETIC);
    EXCEPT(e);
    set_typ(&node->typ, TBI_VOID);
    break;
  default:
    assert(false);
    break;
  }

  return 0;
}

static ERROR type_inference_bin_isa(struct module *mod, struct node *node) {
  error e;
  struct node *left = subs_first(node);
  struct node *right = subs_last(node);
  if (!(left->flags & NODE_IS_TYPE) && typ_is_dyn(left->typ)) {
    set_typ(&node->typ, TBI_BOOL);
    return 0;
  }

  node_subs_remove(node, left);
  node_subs_remove(node, right);

  if (!(node->flags & NODE_IS_TYPE)) {
    if (left->which == IDENT && left->as.IDENT.def != NULL && left->as.IDENT.def->which == DEFNAME) {
      left->as.IDENT.def->as.DEFNAME.may_be_unused = true;
    } else {
      // By SSA, left is side-effect free. If it's not an IDENT (e.g. a
      // field bin acc), we can ignore it.
    }
  }

  const bool isa = typ_isa(left->typ, right->typ);

  if (parent(node)->which == BLOCK && nparent(node, 2)->which == IF) {
    // ssa.c does not replace the result of 'isa' in this case:
    //   if a isa b
    // to allow for implementation selection in generics.
    struct node *par = parent(node);
    struct node *xif = parent(par);
    assert(subs_first(xif) == par && "We should be in the condition block");

    struct node *to_disable = subs_at(xif, isa ? 2 : 1);
    assert(to_disable->which == BLOCK);

    struct node *s = subs_first(to_disable);
    while (s != NULL) {
      struct node *nxt = next(s);
      node_subs_remove(to_disable, s);
      s = nxt;
    }

    (void) mk_node(mod, to_disable, INIT);
  }

  node_set_which(node, BOOL);
  node->as.BOOL.value = isa;

  e = catchup(mod, NULL, node, CATCHUP_REWRITING_CURRENT);
  EXCEPT(e);

  return 0;
}

static ERROR type_inference_bin_rhs_type(struct module *mod, struct node *node) {
  error e;
  struct node *left = subs_first(node);
  struct node *right = subs_last(node);

  if (!(right->flags & NODE_IS_TYPE)) {
    e = mk_except_type(mod, right, "right-hand side not a type");
    THROW(e);
  }

  if (node->as.BIN.operator == Tisa) {
    e = type_inference_bin_isa(mod, node);
    EXCEPT(e);
  } else {
    e = unify(mod, node, left->typ, right->typ);
    EXCEPT(e);

    set_typ(&node->typ, left->typ);
  }

  return 0;
}

static ERROR type_inference_bin(struct module *mod, struct node *node) {
  assert(node->which == BIN);

  error e;
  enum token_type op = node->as.BIN.operator;
  switch (OP_KIND(op)) {
  case OP_BIN_SYM:
  case OP_BIN_SYM_BOOL:
  case OP_BIN_SYM_ADDARITH:
  case OP_BIN_SYM_ARITH:
  case OP_BIN_SYM_INTARITH:
  case OP_BIN_SYM_OVARITH:
  case OP_BIN_SYM_BW:
  case OP_BIN_SYM_PTR:
    e = check_terms_not_types(mod, node);
    EXCEPT(e);
    return type_inference_bin_sym(mod, node);
  case OP_BIN_BW_RHS_UNSIGNED:
  case OP_BIN_OVBW_RHS_UNSIGNED:
    e = check_terms_not_types(mod, node);
    EXCEPT(e);
    return type_inference_bin_rhs_unsigned(mod, node);
  case OP_BIN_ACC:
    return type_inference_bin_accessor(mod, node);
  case OP_BIN_RHS_TYPE:
    return type_inference_bin_rhs_type(mod, node);
  default:
    assert(false);
    return 0;
  }
}

static ERROR typ_tuple(struct typ **result, struct module *mod, struct node *node) {
  const size_t arity = subs_count(node);
  struct typ **args = calloc(arity, sizeof(struct typ *));
  size_t n = 0;
  FOREACH_SUB(s, node) {
    args[n] = s->typ;
    n += 1;
  }

  error e = instantiate(result, mod, node, 0,
                        typ_lookup_builtin_tuple(mod, arity), args, arity, false);
  EXCEPT(e);

  free(args);

  return 0;
}

static ERROR type_inference_tuple(struct module *mod, struct node *node) {
  size_t n = 0;
  FOREACH_SUB(s, node) {
    const uint32_t s_flags = (s->which == DEFARG ? subs_last(s) : s)->flags;
    if (n > 0 && (node->flags & NODE_IS_TYPE) != (s_flags & NODE_IS_TYPE)) {
      error e = mk_except_type(mod, s, "tuple combines values and types");
      THROW(e);
    }
    node->flags |= (s_flags & NODE__TRANSITIVE);
    n += 1;
  }

  struct typ *i = NULL;
  error e = typ_tuple(&i, mod, node);
  EXCEPT(e);

  set_typ(&node->typ, i);

  return 0;
}

static ERROR type_inference_init_named(struct module *mod, struct node *node) {
  struct node *dinc = defincomplete_create(mod, node);

  if (node->as.INIT.is_defchoice_external_payload_constraint) {
    struct node *v = subs_first(node);
    defincomplete_add_field(mod, v, dinc, node->as.INIT.for_tag, v->typ);
  } else {
    FOREACH_SUB_EVERY(s, node, 0, 2) {
      const struct node *left = s;
      const struct node *right = next(s);
      defincomplete_add_field(mod, s, dinc, node_ident(left), right->typ);
    }
  }

  error e = defincomplete_catchup(mod, dinc);
  EXCEPT(e);
  set_typ(&node->typ, dinc->typ);

  if (node->as.INIT.is_range) {
    e = unify(mod, node, node->typ, TBI_RANGE);
    EXCEPT(e);
  } else if (node->as.INIT.is_bounds) {
    e = unify(mod, node, node->typ, TBI_BOUNDS);
    EXCEPT(e);
  } else if (node->as.INIT.is_optional) {
    struct node *x = subs_at(node, 1);
    struct typ *i = NULL;
    e = optional(&i, mod, node, x->typ);
    EXCEPT(e);
    e = unify(mod, node, node->typ, i);
    EXCEPT(e);
    node->flags |= x->flags & NODE__TRANSITIVE;
  }

  return 0;
}

static ERROR type_inference_init_array(struct module *mod, struct node *node) {
  set_typ(&node->typ, create_tentative(mod, node, TBI_LITERALS_SLICE));

  FOREACH_SUB(s, node) {
    error e = unify(mod, s, s->typ,
                    typ_generic_arg(node->typ, 0));
    EXCEPT(e);
  }

  return 0;
}

static ERROR type_inference_init_isalist_literal(struct module *mod, struct node *node) {
  struct node *dinc = defincomplete_create(mod, node);

  FOREACH_SUB(s, node) {
    defincomplete_add_isa(mod, s, dinc, s->typ);
  }

  error e = defincomplete_catchup(mod, dinc);
  EXCEPT(e);
  set_typ(&node->typ, dinc->typ);

  node->flags |= NODE_IS_TYPE;
  return 0;
}

static ERROR type_inference_init(struct module *mod, struct node *node) {
  assert(node->which == INIT);
  error e;
  if (node->as.INIT.is_array) {
    if ((subs_first(node)->flags & NODE_IS_TYPE)
        && typ_definition_which(subs_first(node)->typ) == DEFINTF) {
      e = type_inference_init_isalist_literal(mod, node);
      EXCEPT(e);
    } else {
      e = type_inference_init_array(mod, node);
      EXCEPT(e);
    }
  } else {
    e = type_inference_init_named(mod, node);
    EXCEPT(e);
  }

  if (mod->state->fun_state == NULL && !(node->flags & NODE_IS_TYPE)) {
    node->flags |= NODE_IS_GLOBAL_STATIC_CONSTANT;
  }

  return 0;
}

static ERROR flexible_except_return(struct module *mod, struct node *node,
                                   struct typ *ret, struct node *arg) {
  static const char *fmt = "'except' without a surrounding try/catch must be"
    " used in a function that returns either 'Error' or a tuple that starts"
    " with Error, not '%s'";

  error e;
  if (!typ_isa(ret, TBI_ANY_TUPLE)) {
    if (!typ_equal(ret, TBI_ERROR)) {
      e = mk_except_type(mod, arg, fmt, pptyp(mod, ret));
      THROW(e);
    }
    return 0;
  }

  const size_t arity = typ_generic_arity(ret);
  if (arity == 0) {
    e = mk_except_type(mod, arg, fmt, pptyp(mod, ret));
    THROW(e);
  }

  struct typ *fst = typ_generic_arg(ret, 0);
  if (!typ_equal(fst, TBI_ERROR)) {
    e = mk_except_type(mod, arg, fmt, pptyp(mod, ret));
    THROW(e);
  }

  GSTART();
  G0(narg, node, TUPLE,
     node_subs_remove(node, arg);
     node_subs_append(narg, arg));
  for (size_t n = 1; n < arity; ++n) {
    // TODO We are being lenient here, and risk creating uninitialized
    // values. Constraints should be enforcing this.
    G0(init, narg, INIT);
  }
  const struct node *except[] = { arg, NULL };
  e = catchup(mod, except, narg, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);
  return 0;
}

static ERROR type_inference_return(struct module *mod, struct node *node) {
  assert(node->which == RETURN);

  error e;
  if (subs_count_atleast(node, 1)) {
    struct typ *ret = module_retval_get(mod)->typ;
    struct node *arg = subs_first(node);

    // Do not automatically deref. This is considered confusing. At least
    // until we have escape analysis.

    if (node->as.RETURN.is_flexible_except) {
      e = flexible_except_return(mod, node, ret, arg);
      EXCEPT(e);
      // Was just modified:
      arg = subs_first(node);
    } else if (typ_is_optional(ret) && !typ_is_optional(arg->typ)) {
      e = wrap_arg_unary_op(&arg, mod, TPREQMARK);
      EXCEPT(e);
    } else if (typ_is_nullable_reference(ret) && typ_is_nullable_reference(arg->typ)) {
      // noop
    } else if (!typ_is_optional(ret) && typ_is_optional(arg->typ)) {
      e = wrap_arg_unary_op(&arg, mod, T__DEOPT);
      EXCEPT(e);
    }

    e = unify_refcompat(mod, arg, ret, arg->typ);
    EXCEPT(e);
  }

  set_typ(&node->typ, TBI_VOID);

  return 0;
}

static enum token_type refop_for_accop[] = {
  [TDOT] = TREFDOT,
  [TBANG] = TREFBANG,
  [TSHARP] = TREFSHARP,
  [TWILDCARD] = TREFWILDCARD,
};

static enum token_type accop_for_refop[] = {
  [TREFDOT] = TDOT,
  [TREFBANG] = TBANG,
  [TREFSHARP] = TSHARP,
  [TREFWILDCARD] = TWILDCARD,
  [TCREFDOT] = TDOT,
  [TCREFBANG] = TBANG,
  [TCREFSHARP] = TSHARP,
  [TCREFWILDCARD] = TWILDCARD,
};

static enum token_type derefop_for_accop[] = {
  [TDOT] = TDEREFDOT,
  [TBANG] = TDEREFBANG,
  [TSHARP] = TDEREFSHARP,
  [TWILDCARD] = TDEREFWILDCARD,
};

struct node *expr_ref(struct module *mod, struct node *par,
                      enum token_type refop, struct node *node) {
  if (node->which == BIN && OP_KIND(node->as.BIN.operator) == OP_BIN_ACC) {
    // Of the form
    //   self.x.y!method args
    // which was transformed to
    //   type.method @!self.x.y args
    // We actually need
    //   type.method @!self.x!y args
    // This is assuming that typing has checked the transformation below is
    // legal.
    node->as.BIN.operator = accop_for_refop[refop];
  }

  struct node *n = mk_node(mod, par, UN);
  n->codeloc = node->codeloc;
  n->as.UN.operator = refop;
  node_subs_append(n, node);
  return n;
}

static ERROR rewrite_self(struct module *mod, struct node *node,
                          struct node *old_fun) {
  assert(old_fun->which == BIN);
  const ident name_old_fun = typ_definition_ident(old_fun->typ);

  struct node *old_self = subs_first(old_fun);
  struct node *self;
  if (typ_is_reference(old_self->typ)
      // We don't want those coming from autointf.c
      && !(typ_is_counted_reference(old_self->typ)
           && old_fun->as.BIN.is_operating_on_counted_ref
           && (name_old_fun == ID_DTOR
               || name_old_fun == ID_MOVE
               || name_old_fun == ID_COPY_CTOR))) {
    node_subs_remove(old_fun, old_self);
    node_subs_insert_after(node, subs_first(node), old_self);
    self = old_self;
  } else {
    node_subs_remove(old_fun, old_self);
    enum token_type access = refop_for_accop[old_fun->as.BIN.operator];
    struct node *s = expr_ref(mod, node, access, old_self);
    node_subs_remove(node, s);
    node_subs_insert_after(node, subs_first(node), s);
    self = s;
  }

  if (self != old_self) {
    const struct node *except[] = { old_self, NULL };
    error e = catchup(mod, except, self, CATCHUP_BELOW_CURRENT);
    EXCEPT(e);
  }

  if (typ_is_counted_reference(self->typ)
      && (name_old_fun == ID_DTOR
          || name_old_fun == ID_MOVE
          || name_old_fun == ID_COPY_CTOR)) {
    // No need to check, only Move, Copy_Ctor, Dtor operating directly on a
    // counted reference could have made it here.
  } else if (typ_is_reference(self->typ)) {
    error e = typ_check_can_deref(mod, old_fun, self->typ,
                                  derefop_for_accop[old_fun->as.BIN.operator]);
    EXCEPT(e);
  }

  return 0;
}

static bool compare_ref_depth(const struct typ *target, const struct typ *arg,
                              int diff) {
  assert(!typ_equal(target, TBI_ANY_ANY_REF));

  if (typ_equal(arg, TBI_LITERALS_NIL)) {
    return diff != 1;
  }

  int dtarget = 0;
  while (typ_is_reference(target)) {
    dtarget += 1;
    if (typ_equal(target, TBI_ANY_ANY_REF)
        || typ_equal(target, TBI_ANY_ANY_LOCAL_REF)
        || typ_equal(target, TBI_ANY_ANY_COUNTED_REF)) {
      break;
    }
    target = typ_generic_arg_const(target, 0);
  }

  int darg = 0;
  while (typ_is_reference(arg)) {
    darg += 1;
    if (typ_equal(arg, TBI_ANY_ANY_REF)
        || typ_equal(arg, TBI_ANY_ANY_LOCAL_REF)
        || typ_equal(arg, TBI_ANY_ANY_COUNTED_REF)) {
      break;
    }
    arg = typ_generic_arg_const(arg, 0);

    if (typ_equal(arg, TBI_LITERALS_NIL)) {
      return diff != 1;
    }
  }

  return dtarget == darg + diff;
}

static ERROR try_insert_automagic_arg_ref(bool *did_ref, struct node **arg,
                                          struct module *mod, struct node *node,
                                          const struct typ *target,
                                          enum token_type target_explicit_ref) {
  const bool is_named = (*arg)->which == CALLNAMEDARG;
  struct node *real_arg = is_named ? subs_first(*arg) : (*arg);
  struct node *expr_arg = follow_ssa(real_arg);

  if (target_explicit_ref == TREFDOT || target_explicit_ref == TNULREFDOT) {
    if (compare_ref_depth(target, real_arg->typ, 1)) {
      if (!typ_isa(target, TBI_ANY_MREF)) {
        struct node *before = prev(real_arg);

        struct node *par = parent(real_arg);
        node_subs_remove(par, real_arg);
        struct node *ref_arg = expr_ref(mod, par, TREFDOT, real_arg);
        node_subs_remove(par, ref_arg);
        node_subs_insert_after(par, before, ref_arg);

        if (is_named) {
          unset_typ(&(*arg)->typ);
        } else {
          *arg = ref_arg;
        }

        const struct node *except[] = { real_arg, NULL };
        error e = catchup(mod, except, *arg, CATCHUP_BELOW_CURRENT);
        EXCEPT(e);

        *did_ref = true;
      }
    } else if (compare_ref_depth(target, real_arg->typ, 0)) {
      if (expr_arg != NULL
          && expr_arg->which == UN
          && expr_arg->as.UN.operator == TREFDOT
          && expr_arg->as.UN.is_explicit
          && !typ_equal(subs_first(expr_arg)->typ, TBI_LITERALS_NIL)) {
        error e = mk_except_type(mod, expr_arg, "explicit '@' operators are not"
                                 " allowed for unqualified const references");
        THROW(e);
      }
    }
  } else {
    // We do not automagically insert const ref operators when the
    // function does not always accept a reference, as in: (also see
    // t00/automagicref.n)
    //   s (method t:`any) foo p:t = void
    //     noop
    //   (s.foo @i32) self @1 -- '@' required on 1.
    //   (s.foo i32) self 1

    // noop -- this error will get caught by regular argument typing.
  }

  return 0;
}

static ERROR try_insert_automagic_arg(bool *did_ref, struct node **arg,
                                      struct module *mod, struct node *node,
                                      const struct typ *target,
                                      enum token_type target_explicit_ref) {
  if (typ_equal((*arg)->typ, TBI_LITERALS_NIL)) {
    return 0;
  }

  if (typ_is_reference(target) && !typ_is_optional((*arg)->typ)) {
    error e = try_insert_automagic_arg_ref(did_ref, arg, mod, node, target, target_explicit_ref);
    EXCEPT(e);
  } else if (typ_is_optional(target) && !typ_is_optional((*arg)->typ)) {
    error e = wrap_arg_unary_op(arg, mod, TPREQMARK);
    EXCEPT(e);
  }
  return 0;
}

static ERROR try_insert_automagic_arg_de(struct node **arg,
                                         struct module *mod,
                                         const struct typ *target,
                                         enum token_type target_explicit_ref) {
  if (!typ_is_optional((*arg)->typ)) {
    if (target_explicit_ref != 0 || typ_is_reference(target)) {
      return 0;
    }
  }

  // target can be either t or ?t
  // arg can be any of: ?t, @t, @?t
  // Don't deref more than once.
  bool is_nil = typ_equal((*arg)->typ, TBI_LITERALS_NIL);
  if (!typ_is_optional(target) && !is_nil && typ_is_optional((*arg)->typ)) {
    error e = wrap_arg_unary_op(arg, mod, T__DEOPT);
    EXCEPT(e);
  }
  is_nil = typ_equal((*arg)->typ, TBI_LITERALS_NIL);
  if (!typ_is_reference(target) && !is_nil && typ_is_reference((*arg)->typ)) {
    error e = wrap_arg_unary_op(arg, mod, TDEREFDOT);
    EXCEPT(e);
  }
  is_nil = typ_equal((*arg)->typ, TBI_LITERALS_NIL);
  if (!typ_is_optional(target) && !is_nil && typ_is_optional((*arg)->typ)) {
    error e = wrap_arg_unary_op(arg, mod, T__DEOPT);
    EXCEPT(e);
  }
  return 0;
}

static ERROR process_automagic_call_arguments(struct module *mod,
                                              struct node *node,
                                              const struct typ *tfun) {
  if (!subs_count_atleast(node, 2)) {
    return 0;
  }

  const ssize_t first_vararg = typ_function_first_vararg(tfun);

  error e;
  ssize_t n = 0;
  struct node *last = NULL;
  struct node *nxt = subs_at(node, 1);
  while (nxt != NULL) {
    if (n == first_vararg) {
      break;
    }

    // We record 'nxt' now as try_insert_automagic_arg{,_de}() may move 'arg'.
    struct node *arg = nxt;
    nxt = next(nxt);

    const enum token_type explicit_ref = typ_function_arg_explicit_ref(tfun, n);
    bool did_ref = false;
    e = try_insert_automagic_arg(&did_ref, &arg, mod, node,
                                 typ_function_arg_const(tfun, n),
                                 explicit_ref);
    EXCEPT(e);

    e = try_insert_automagic_arg_de(&arg, mod, typ_function_arg_const(tfun, n),
                                    explicit_ref);
    EXCEPT(e);

    if (!did_ref) {
      e = try_insert_automagic_arg(&did_ref, &arg, mod, node,
                                   typ_function_arg_const(tfun, n),
                                   explicit_ref);
      EXCEPT(e);
    }

    n += 1;
    last = arg;
  }

  if (n == first_vararg) {
    const struct typ *target = typ_generic_arg_const(
      typ_function_arg_const(tfun, n), 0);

    struct node *nxt = last == NULL
      ? next(subs_first(node)) : next(last);
    while (nxt != NULL) {
      // We record 'nxt' now as try_insert_automagic_arg() may move 'arg'.
      struct node *arg = nxt;
      nxt = next(nxt);

      bool did_ref = false;
      e = try_insert_automagic_arg(&did_ref, &arg, mod, node, target, TREFDOT);
      EXCEPT(e);

      e = try_insert_automagic_arg_de(&arg, mod, target, TREFDOT);
      EXCEPT(e);

      if (!did_ref) {
        e = try_insert_automagic_arg(&did_ref, &arg, mod, node, target, TREFDOT);
        EXCEPT(e);
      }
    }
  }

  return 0;
}

static ERROR prepare_call_arguments(struct module *mod, struct node *node) {
  error e;
  struct node *fun = subs_first(node);
  struct typ *tfun = fun->typ;
  const size_t tmin = typ_function_min_arity(tfun);
  const size_t tmax = typ_function_max_arity(tfun);

  const size_t args = subs_count(node) - 1;

  switch (typ_definition_which(tfun)) {
  case DEFFUN:
    if (args < tmin || args > tmax) {
      e = mk_except_call_args_count(mod, node, tfun, false, args);
      THROW(e);
    }
    break;
  case DEFMETHOD:
    if (fun->which == BIN) {
      if ((subs_first(fun)->flags & NODE_IS_TYPE)) {
        // Form (type.method self ...).
        if (args < tmin || args > tmax) {
          e = mk_except_call_args_count(mod, node, tfun, false, args);
          THROW(e);
        }
      } else {
        // Form (self.method ...); rewrite as (type.method self ...).
        if (args+1 < tmin || args+1 > tmax) {
          e = mk_except_call_args_count(mod, node, tfun, true, args);
          THROW(e);
        }

        struct node *m = mk_node(mod, node, DIRECTDEF);
        set_typ(&m->as.DIRECTDEF.typ, tfun);
        m->as.DIRECTDEF.flags = NODE_IS_TYPE;
        node_subs_remove(node, m);
        node_subs_replace(node, fun, m);
        e = catchup(mod, NULL, m, CATCHUP_BELOW_CURRENT);
        EXCEPT(e);

        e = rewrite_self(mod, node, fun);
        EXCEPT(e);

        e = catchup(mod, NULL, m, CATCHUP_BELOW_CURRENT);
        EXCEPT(e);
      }
    } else if ((fun->flags & NODE_IS_TYPE)) {
      assert(fun->which == CALL || fun->which == DIRECTDEF);
      // Generic method instantiation: (type.method u32 i32) self
      // or DIRECTDEF.
      if (args < tmin || args > tmax) {
        e = mk_except_call_args_count(mod, node, tfun, false, args);
        THROW(e);
      }
    } else {
      assert(false && "Unreached");
    }
    break;
  default:
    assert(false);
  }

  e = fill_in_optional_args(mod, node, tfun);
  EXCEPT(e);

  e = process_automagic_call_arguments(mod, node, tfun);
  EXCEPT(e);

  return 0;
}

static ERROR explicit_instantiation(struct module *mod, struct node *node) {
  error e;
  struct node *what = subs_first(node);
  struct typ *t = what->typ;
  if (!typ_is_function(t) && !typ_is_generic_functor(t)) {
    e = mk_except_type(mod, what,
                       "explicit generic instantion must use a functor");
    THROW(e);
  }

  if (mod->state->top_state->is_setgenarg) {
    t = typ_create_tentative_functor(mod, t);
  }

  const size_t given_arity = subs_count(node) - 1;

  const size_t arity = typ_generic_arity(t);
  const size_t first_explicit = typ_generic_first_explicit_arg(t);
  const size_t explicit_arity = arity - first_explicit;
  if (given_arity > explicit_arity) {
    e = mk_except_type(mod, node,
                       "invalid number of explicit generic arguments:"
                       " %zu expected, but %zu given",
                       explicit_arity, given_arity);
    THROW(e);
  }

  struct typ *i = NULL;
  if (first_explicit == 0) {
    struct typ **args = calloc(arity, sizeof(struct typ *));
    size_t n = 0;
    FOREACH_SUB_EVERY(s, node, 1, 1) {
      args[n] = s->typ;
      n += 1;
    }
    // Ommitted generic arguments.
    for (; n < arity; ++n) {
      args[n] = tentative_generic_arg(mod, node, t, n);
    }

    e = instantiate(&i, mod, node, 1, t, args, arity, false);
    EXCEPT(e);

    free(args);
    set_typ(&node->typ, i);
  } else {
    i = instantiate_fully_implicit(mod, node, t);
    set_typ(&node->typ, i);

    size_t n = first_explicit;
    FOREACH_SUB_EVERY(s, node, 1, 1) {
      e = unify(mod, s, typ_generic_arg(node->typ, n), s->typ);
      EXCEPT(e);
      n += 1;
    }
  }

  topdeps_record(mod, node->typ);
  node->flags |= NODE_IS_TYPE;

  return 0;
}

static ERROR link_wildcard_generics(struct module *mod, struct typ *i,
                                    struct node *call) {
  error e;
  if (typ_definition_which(i) == DEFMETHOD
      && typ_definition_defmethod_access_is_wildcard(i)) {
    struct node *self_node = subs_at(call, 1);
    struct typ *self = self_node->typ;
    struct typ *self0 = typ_generic_functor(self);

    struct node *self_def = NULL;
    struct typ *pre_conv_cref0 = NULL;
    if (self_node->which == IDENT
        && (self_def = self_node->as.IDENT.def)->which == DEFNAME
        && self_def->which == DEFNAME
        && self_def->as.DEFNAME.ssa_user == self_node) {
      struct node *self_expr = subs_last(self_def);
      if (self_expr->which == CONV) {
        struct typ *pre_conv = subs_first(self_expr)->typ;
        if (typ_is_counted_reference(pre_conv)) {
          // See implicit_function_instantiation() CONV early insertion.
          pre_conv_cref0 = typ_generic_functor(pre_conv);
        }
      }
    }

    struct typ *wildcard = typ_definition_defmethod_wildcard_functor(i);
    struct typ *self_wildcard = typ_definition_defmethod_self_wildcard_functor(i);
    struct typ *nullable_wildcard = typ_definition_defmethod_nullable_wildcard_functor(i);

    e = unify(mod, call, self_wildcard, self0);
    EXCEPT(e);
    // Update after the link.
    self_wildcard = typ_definition_defmethod_self_wildcard_functor(i);

    if (pre_conv_cref0 != NULL) {
      struct typ *ref0 = NULL;
      if (typ_equal(pre_conv_cref0, TBI_CREF)) {
        ref0 = TBI_REF;
      } else if (typ_equal(pre_conv_cref0, TBI_CMREF)) {
        ref0 = TBI_MREF;
      } else if (typ_equal(pre_conv_cref0, TBI_CMMREF)) {
        ref0 = TBI_MMREF;
      } else if (typ_equal(pre_conv_cref0, TBI_CNREF)) {
        ref0 = TBI_NREF;
      } else if (typ_equal(pre_conv_cref0, TBI_CNMREF)) {
        ref0 = TBI_NMREF;
      } else if (typ_equal(pre_conv_cref0, TBI_CNMMREF)) {
        ref0 = TBI_NMMREF;
      }

      e = unify(mod, call, self_wildcard, ref0);
      EXCEPT(e);
      self_wildcard = typ_definition_defmethod_self_wildcard_functor(i);
    }

    if ((typ_toplevel_flags(i) & TOP_IS_SHALLOW) && typ_equal(self0, TBI_MREF)) {
      e = unify(mod, call, wildcard, TBI_MMREF);
      EXCEPT(e);
      e = unify(mod, call, nullable_wildcard, TBI_NMMREF);
      EXCEPT(e);
    } else if ((typ_toplevel_flags(i) & TOP_IS_SHALLOW) && typ_equal(self0, TBI_CMREF)) {
      e = unify(mod, call, wildcard, TBI_CMMREF);
      EXCEPT(e);
      e = unify(mod, call, nullable_wildcard, TBI_CNMMREF);
      EXCEPT(e);
    } else {
      e = unify(mod, call, wildcard, self_wildcard);
      EXCEPT(e);
      // Update after the link.
      wildcard = typ_definition_defmethod_wildcard_functor(i);
      e = unify(mod, call, nullable_wildcard, nullable_functor(wildcard));
      EXCEPT(e);
    }
  } else if (typ_definition_which(i) == DEFFUN
             && typ_definition_deffun_access_is_wildcard(i)) {
    struct typ *wildcard = typ_definition_deffun_wildcard_functor(i);
    struct typ *nullable_wildcard = typ_definition_deffun_nullable_wildcard_functor(i);

    if (!typ_is_tentative(wildcard)) {
      e = unify(mod, call, nullable_wildcard, nullable_functor(wildcard));
      EXCEPT(e);
    } else if (!typ_is_tentative(nullable_wildcard)) {
      e = unify(mod, call, wildcard, nonnullable_functor(nullable_wildcard));
      EXCEPT(e);
    } else {
      // This is most likely an unintentional error, e.g.:
      //  let seeker = (Dyncast fs.`Seeker) r
      // with 'fs' not imported, i.e.
      //    IDENT/0 (fs) :*_Ng_1bb{"fs" `Seeker:*(*n.builtins.`Any_ref *n.builtins.`Any) }
      // but it could also be a case of concrete type constraints coming too
      // late.
      e = mk_except_type(mod, call, "under-constrained generic instantiation"
                         " (hints: missing import? undefined ident?)");
      THROW(e);
    }
  }
  return 0;
}

static ERROR implicit_function_instantiation(struct module *mod, struct node *node) {
  error e;
  struct node *fun = subs_first(node);
  struct typ *tfun = fun->typ;

  struct typ **i = typ_permanent_loc(instantiate_fully_implicit(mod, node, tfun));

  size_t n = 0;
  FOREACH_SUB_EVERY(s, node, 1, 1) {
    struct typ *arg = typ_function_arg(*i, n);

    if (typ_isa(arg, TBI_ANY_LOCAL_REF)
        && typ_is_counted_reference(s->typ)) {
      // We need to insert any necessary CONV now. Consider the case
      //    function foo s:@Stringbuf = []U8
      //      return s.Bytes
      // Stringbuf.Bytes is a wildcard method on uncounted references. The
      // unify_refcompat() below would try to unify the (`Any_local_ref Stringbuf)
      // from the wildcard with the type of s:(Cref Stringbuf). While these
      // two references are indeed (ref-)compatible, we do not want to unify
      // `Any_local_ref to Cref, as Bytes is met$, not met@$ or met$@$.
      //
      // CONV are usually inserted much later, after inference. In this case
      // we need to do it right away.
      //
      // The unification of the reference functors on both sides of the CONV
      // is completed in link_wildcard_generics().

      // First unify the arguments themselves.
      e = unify(mod, s, typ_generic_arg(arg, 0), typ_generic_arg(s->typ, 0));
      EXCEPT(e);
      // May have changed:
      arg = typ_function_arg(*i, n);

      struct node *expr = s;
      e = insert_conv(&expr, mod, node, arg);
      EXCEPT(e);

      s = expr;
      // May have changed:
      arg = typ_function_arg(*i, n);
    }

    e = unify_refcompat(mod, s, arg, s->typ);
    EXCEPT(e);

    n += 1;
  }

  e = link_wildcard_generics(mod, *i, node);
  EXCEPT(e);

  // We're turning 'fun' into something different: it was a
  // functor, now it's an instantiated function. We are, in essence,
  // removing the node and replacing with a different one. We don't actually
  // bother to do that, but we need to be careful:
  unset_typ(&fun->typ);
  set_typ(&fun->typ, *i);
  if (fun->which == DIRECTDEF) {
    unset_typ(&fun->as.DIRECTDEF.typ);
    set_typ(&fun->as.DIRECTDEF.typ, *i);
  }

  topdeps_record(mod, *i);
  set_typ(&node->typ, typ_function_return(*i));

  return 0;
}

static ERROR function_instantiation(struct module *mod, struct node *node) {
  error e = implicit_function_instantiation(mod, node);
  EXCEPT(e);

  return 0;
}

static ERROR check_consistent_either_types_or_values(struct module *mod,
                                                     struct node *arg0) {
  uint32_t flags = 0;
  for (struct node *s = arg0; s != NULL; s = next(s)) {
    if (s != arg0 && (flags & NODE_IS_TYPE) != (s->flags & NODE_IS_TYPE)) {
      error e = mk_except_type(mod, s, "expression combines types and values");
      THROW(e);
    }
    flags |= s->flags;
  }

  return 0;
}

static ERROR type_inference_explicit_unary_call(struct module *mod, struct node *node) {
  struct typ **ft = typ_permanent_loc(subs_first_const(node)->typ);

  const size_t count = subs_count(node);
  if (typ_definition_which(*ft) == DEFFUN && count != 1) {
    error e = mk_except_call_args_count(mod, node, *ft, false, count - 1);
    THROW(e);
  } else if (typ_definition_which(*ft)== DEFMETHOD && count != 2) {
    error e = mk_except_call_args_count(mod, node, *ft, false, count - 1);
    THROW(e);
  }

  enum node_which ft_which = typ_definition_which(*ft);
  const ident ft_name = typ_definition_ident(*ft);
  if (ft_which == DEFMETHOD) {
    struct node *self = subs_at(node, 1);

    error e = unify_refcompat(mod, self, typ_function_arg(*ft, 0), self->typ);
    EXCEPT(e);

    if (ft_name == ID_MOVE) {
      // FIXME(e): Properly detect that it's actually `Any.Move
      node->flags |= NODE_IS_MOVE_TARGET;
    }
  }

  set_typ(&node->typ, typ_function_return(*ft));

  return 0;
}

static ERROR try_rewrite_operator_sub_bounds(struct module *mod, struct node *node) {
  if (!subs_count_atleast(node, 3)) {
    return 0;
  }

  struct node *fun = subs_first(node);
  struct node *self = subs_at(node, 1);
  struct node *arg = subs_at(node, 2);

  if (!typ_equal(arg->typ, TBI_BOUNDS)
      || typ_definition_ident(fun->typ) != ID_OPERATOR_SUB) {
    return 0;
  }

  assert(typ_is_reference(self->typ));
  struct tit *m = typ_definition_one_member(typ_generic_arg(self->typ, 0),
                                            ID_OPERATOR_SUB);
  if (m == NULL) {
    error e = mk_except_type(mod, node, "type '%s' does not have '%s'",
                             pptyp(mod, self->typ),
                             idents_value(mod->gctx, ID_OPERATOR_SUB));
    THROW(e);
  }

  unset_typ(&fun->typ);
  unset_typ(&fun->as.DIRECTDEF.typ);
  set_typ(&fun->as.DIRECTDEF.typ, tit_typ(m));
  tit_next(m);

  error e = catchup(mod, NULL, fun, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);

  // Magically convert v.[10...] to v.[(10...).Range_of v].
  node_subs_remove(node, arg);
  GSTART();
  G0(call, node, CALL,
     G(b, BIN,
       b->as.BIN.operator = TDOT;
       node_subs_append(b, arg);
       G_IDENT(f, "Range_of"));

     // By SSA, self is either an IDENT, or if it's a BIN, it's a
     // straight accessor, so we're allowed to duplicate it.
     G(copy_self, 0);
     node_deepcopy(mod, copy_self, self));

  const struct node *except[] = { arg, NULL };
  e = catchup(mod, except, call, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);

  return 0;
}

static bool is_vararg_passdown(struct typ *arg) {
  if (!typ_is_reference(arg)) {
    return false;
  }
  struct typ *va = typ_generic_arg(arg, 0);
  return typ_generic_arity(va) > 0
    && typ_equal(typ_generic_functor(va), TBI_VARARG);
}

static ERROR append_globalenv_refs(struct module *mod, struct node *node,
                                   struct node *globalenv, struct node *within) {
  assert(globalenv->which == DEFNAME);
  assert(within->which == WITHIN);

  struct node *p = subs_first(within);
  const ident env_name = p->which == BIN ? node_ident(subs_last(p)) : node_ident(p);

  GSTART();
  G0(r, node, UN,
     r->as.UN.operator = TREFSHARP;
     G(path_header, 0));
  G0(r2, node, UN,
     r2->as.UN.operator = TREFSHARP;
     G(path, IDENT,
       path->as.IDENT.name = env_name));

  node_deepcopy(mod, path_header, p);

  // Build the path to get to the globalenv header, using 'p', as it gets
  // us to the globalenv itself.
  const ident header = globalenv->as.DEFNAME.globalenv_header_name;
  switch (path_header->which) {
  case IDENT:
    path_header->as.IDENT.name = header;
    break;
  case BIN:
    subs_last(path_header)->as.IDENT.name = header;
    break;
  default:
    assert(false);;
  }
  return 0;
}

static ERROR try_rewrite_globalenv_call(bool *yes,
                                        struct module *mod, struct node *node) {
  error e;
  struct node *fun = subs_first(node);
  const ident name = node_ident(fun);
  ident internal_name = ID__NONE;
  size_t arity = 1;
  switch (name) {
  case ID_TBI_GLOBALENV_INSTALLED: internal_name = ID_INTERNAL_GLOBALENV_INSTALLED; break;
  case ID_TBI_GLOBALENV_PARENT: internal_name = ID_INTERNAL_GLOBALENV_PARENT; break;
  case ID_TBI_GLOBALENV_INSTALL: internal_name = ID_INTERNAL_GLOBALENV_INSTALL; arity = 2; break;
  case ID_TBI_GLOBALENV_UNINSTALL: internal_name = ID_INTERNAL_GLOBALENV_UNINSTALL; break;
  default: *yes = false; return 0;
  }

  struct node *genv = subs_at(node, 1);
  if (genv->which != IDENT || genv->as.IDENT.def == NULL
      || genv->as.IDENT.def->which != WITHIN) {
    e = mk_except(mod, node, "function '%s' must be used on a globalenv"
                  " in a local within declaration",
                  idents_value(mod->gctx, name));
  }
  struct node *within = genv->as.IDENT.def;

  GSTART();
  G0(new_fun, node, IDENT,
     new_fun->as.IDENT.name = internal_name;
     new_fun->typ = TBI__CALL_FUNCTION_SLOT);
  node_subs_remove(node, new_fun);
  node_subs_replace(node, fun, new_fun);

  struct node *globalenv = NULL;
  e = scope_lookup_ident_immediate(&globalenv, node, mod,
                                   genv->as.IDENT.non_local_scoper,
                                   node_ident(genv), false);
  EXCEPT(e);

  assert(globalenv->which == DEFNAME);
  e = append_globalenv_refs(mod, node, globalenv, within);
  EXCEPT(e);

  const struct node *except[] = { genv, NULL, NULL };
  if (arity == 2) {
    except[1] = subs_at(node, 2);
  }
  e = catchup(mod, except, node, CATCHUP_BELOW_CURRENT);
  EXCEPT(e);

  *yes = true;
  return 0;
}

static ERROR type_inference_call(struct module *mod, struct node *node) {
  error e;
  struct node *fun = subs_first(node);

  if (!typ_is_function(fun->typ)
      || (subs_count_atleast(node, 2) && (subs_at(node, 1)->flags & NODE_IS_TYPE))) {
    // Uninstantiated generic, called on types.

    if (!typ_is_function(fun->typ) && typ_generic_arity(fun->typ) == 0) {
      char *n = pptyp(mod, fun->typ);
      e = mk_except_type(mod, fun, "'%s' not a function or a generic", n);
      free(n);
      THROW(e);
    }

    e = explicit_instantiation(mod, node);
    EXCEPT(e);

    if ((typ_definition_which(node->typ) == DEFFUN && typ_function_min_arity(node->typ) == 0)
        && parent_const(node)->which != CALL) {
      // Combined explicit instantiation and unary call:
      //   let p = Alloc I32
      struct node *moved = node_new_subnode(mod, node);
      node_subs_remove(node, moved);
      node_move_content(moved, node);
      node_subs_append(node, moved);
      node->which = CALL;

      e = type_inference_call(mod, node);
      EXCEPT(e);

    } else if ((typ_definition_which(node->typ) == DEFMETHOD && typ_function_min_arity(node->typ) == 1)
               && parent_const(node)->which != CALL) {
      // Combined explicit instantiation and unary call on an object.
      struct node *self = subs_first(fun);
      node_subs_remove(fun, self);

      // Remove type arguments.
      struct node *nxt = next(fun);
      while (nxt != NULL) {
        struct node *del = nxt;
        nxt = next(nxt);
        node_subs_remove(node, del);
      }

      GSTART();
      G0(f, node, DIRECTDEF,
         f->as.DIRECTDEF.flags = NODE_IS_TYPE;
         set_typ(&f->as.DIRECTDEF.typ, node->typ));
      node_subs_remove(node, f);
      node_subs_replace(node, fun, f);
      node_subs_append(node, self);

      e = catchup(mod, NULL, f, CATCHUP_BELOW_CURRENT);
      EXCEPT(e);

      node_set_which(node, CALL);
      node->flags &= ~NODE_IS_TYPE;

      e = type_inference_call(mod, node);
      EXCEPT(e);
    }

    return 0;
  }

  e = prepare_call_arguments(mod, node);
  EXCEPT(e);

  // Assumes that operator_at and operator_sub have the same arguments.
  e = try_rewrite_operator_sub_bounds(mod, node);
  EXCEPT(e);

  e = check_consistent_either_types_or_values(mod, try_node_subs_at(node, 1));
  EXCEPT(e);

  bool done = false;
  e = try_rewrite_globalenv_call(&done, mod, node);
  EXCEPT(e);
  if (done) {
    return 0;
  }

  if (typ_is_generic_functor(fun->typ)) {
    // Uninstantiated generic, called on values.

    e = function_instantiation(mod, node);
    EXCEPT(e);

    return 0;
  }

  if (typ_function_max_arity(fun->typ)
      == (typ_definition_which(fun->typ) == DEFMETHOD ? 1 : 0)) {
    return type_inference_explicit_unary_call(mod, node);
  }

  const ssize_t first_vararg = typ_function_first_vararg(fun->typ);
  ssize_t n = 0;
  FOREACH_SUB_EVERY(arg, node, 1, 1) {
    if (n == first_vararg) {
      break;
    }
    e = unify_refcompat(mod, arg, typ_function_arg(fun->typ, n), arg->typ);
    EXCEPT(e);
    n += 1;
  }

  if (n == first_vararg) {
    FOREACH_SUB_EVERY(arg, node, 1 + n, 1) {
      struct typ *target = typ_generic_arg(typ_function_arg(fun->typ, n), 0);

      if (arg->which == CALLNAMEDARG && arg->as.CALLNAMEDARG.is_slice_vararg) {
        if (is_vararg_passdown(arg->typ)) {
          e = unify_refcompat(mod, arg, target, typ_generic_arg(typ_generic_arg(arg->typ, 0), 0));
          EXCEPT(e);
        } else {
          struct typ *target_arg = typ_generic_arg(target, 0);
          assert(typ_definition_which(target_arg) != DEFINTF && "Unsupported: needs dyn slice support");

          struct typ *slice_target = NULL;
          e = instantiate(&slice_target, mod, arg, -1, TBI_SLICE, &target_arg, 1, false);
          EXCEPT(e);

          e = unify_refcompat(mod, arg, slice_target, typ_generic_arg(arg->typ, 0));
          EXCEPT(e);
        }
        break;
      }

      e = unify_refcompat(mod, arg, target, arg->typ);
      EXCEPT(e);
    }
  }

  FOREACH_SUB(arg, node) {
    topdeps_record(mod, arg->typ);
  }

  struct typ **ft = typ_permanent_loc(fun->typ);
  e = link_wildcard_generics(mod, *ft, node);
  EXCEPT(e);

  set_typ(&node->typ, typ_function_return(*ft));

  return 0;
}

static ERROR type_inference_block(struct module *mod, struct node *node) {
  error e;

  struct node *last_typable = subs_last(node);
  while (typ_equal(last_typable->typ, TBI__NOT_TYPEABLE)) {
    last_typable = prev(last_typable);
  }

  FOREACH_SUB(s, node) {
    if ((s->flags & NODE_IS_TYPE)) {
      e = mk_except_type(mod, s, "block statements cannot be type names");
      THROW(e);
    }

    if (s->typ == TBI__NOT_TYPEABLE) {
      continue;
    }

    if (s == last_typable) {
      break;
    }

    if (!typ_equal(s->typ, TBI_VOID)) {
      e = mk_except_type(mod, s,
                         "intermediate statements in a block must be of type void"
                         " (except the last one), not '%s'",
                         pptyp(mod, s->typ));
      THROW(e);
    }
  }

  if (last_typable->which == RETURN) {
    // FIXME: should make sure there are no statements after a RETURN.
    set_typ(&node->typ, TBI_VOID);
  } else {
    set_typ(&node->typ, last_typable->typ);
  }

  return 0;
}

static ERROR type_inference_if(struct module *mod, struct node *node) {
  error e;

  struct node *cond = subs_first(node);
  e = try_insert_automagic_de(mod, cond);
  EXCEPT(e);
  // may have been modified, get the new one
  cond = subs_first(node);

  e = unify(mod, cond, cond->typ,
            create_tentative(mod, node, TBI_GENERALIZED_BOOLEAN));
  EXCEPT(e);

  struct node *yes = next(cond);
  struct node *els = subs_last(node);
  e = unify(mod, els, yes->typ, els->typ);
  EXCEPT(e);

  set_typ(&node->typ, yes->typ);

  return 0;
}

static bool in_a_body_pass(struct module *mod) {
  return mod->stage->state->passing >= PASSZERO_COUNT + PASSFWD_COUNT;
}

static ERROR type_inference_ident_unknown(struct module *mod, struct node *node) {
  error e;
  if (!in_a_body_pass(mod)) {
    e = mk_except(mod, node, "unknown ident '%s'",
                  idents_value(mod->gctx, node_ident(node)));
    THROW(e);
  }

  struct node *unk = defincomplete_create(mod, node);
  defincomplete_set_ident(mod, node, unk, node_ident(node));
  e = defincomplete_catchup(mod, unk);
  EXCEPT(e);

  // Special marker, so we can rewrite it with the final enum or sum scope
  // in step_ident_non_local_scope().
  node->as.IDENT.non_local_scoper = unk;

  set_typ(&node->typ, unk->typ);
  return 0;
}

static ERROR type_inference_ident(struct module *mod, struct node *node) {
  if (node_is_name_of_globalenv(node)) {
    set_typ(&node->typ, create_tentative(mod, node, TBI_ANY));
    return 0;
  }

  if (node_ident(node) == ID_OTHERWISE) {
    set_typ(&node->typ, create_tentative(mod, node, TBI_ANY));
    return 0;
  }

  struct node *def = NULL;
  error e = scope_statement_lookup(&def, mod, node, node, true);
  if (e == EINVAL) {
    e = type_inference_ident_unknown(mod, node);
    EXCEPT(e);
    return 0;
  }

  if (def->which == CATCH) {
    // 'node' is a throw or except label.
    return 0;
  }

  if (def->typ == NULL) {
    if (def->which == DEFALIAS && node_is_at_top(parent_const(def))) {
      // Opportunistically type this alias
      e = step_type_aliases(node_module_owner(def), parent(def), NULL, NULL);
      EXCEPT(e);
    } else {
      e = mk_except(mod, node,
                    "identifier '%s' used before its definition",
                    idents_value(mod->gctx, node_ident(node)));
      THROW(e);
    }
  }

  node->as.IDENT.def = def;
  if (def->flags & NODE_IS_GLOBAL_LET) {
    node->as.IDENT.non_local_scoper = nparent(def, 2);
    topdeps_record_global(mod, def);
  } else if (def->which == WITHIN) {
    node->as.IDENT.non_local_scoper = def->as.WITHIN.globalenv_scoper;
  } else if (def->which == DEFCHOICE) {
    node->as.IDENT.non_local_scoper = typ_definition_ignore_any_overlay(def->typ);
  }

  if (typ_is_function(def->typ) && node->typ != TBI__CALL_FUNCTION_SLOT) {
    if (typ_function_min_arity(def->typ) != 0) {
      e = mk_except_call_args_count(mod, node, def->typ, false, 0);
      THROW(e);
    }

    e = rewrite_unary_call(mod, node, def->typ);
    EXCEPT(e);
  } else {
    set_typ(&node->typ, def->typ);
    node->flags = def->flags;
  }

  return 0;
}

static struct typ* number_literal_typ(struct module *mod, struct node *node) {
  assert(node->which == NUMBER);
  if (strchr(node->as.NUMBER.value, '.') != NULL) {
    return TBI_LITERALS_FLOATING;
  } else {
    return TBI_LITERALS_INTEGER;
  }
}

static ERROR type_inference_within(struct module *mod, struct node *node) {
  // The subtree below 'node' has been marked TBI__NOT_TYPEABLE.
  node->typ = NULL;

  error e;
  struct node *def = NULL;
  struct node *first = subs_first(node);

  if (node->which == WITHIN) {
    const struct node *modbody = NULL;
    struct node *name = NULL;
    if (first->which == BIN) {
      struct node *ffirst = subs_first(first);
      e = type_inference_within(mod, ffirst);
      EXCEPT(e);

      const struct node *nmod = typ_definition_nooverlay_const(ffirst->typ);
      if (nmod->which != MODULE) {
        e = mk_except(mod, node, "invalid within expression,"
                      " must point to a globalenv declaration, inside a moduel");
        THROW(e);
      }
      modbody = nmod->as.MODULE.mod->body;
      name = subs_last(first);;
    } else if (first->which == IDENT) {
      modbody = node_module_owner_const(node)->body;
      name = first;
    } else {
      goto malformed;
    }

    e = scope_lookup_ident_immediate(&def, node, mod,
                                     modbody->as.MODULE_BODY.globalenv_scoper,
                                     node_ident(name), false);
    if (e) {
      e = mk_except(mod, node, "in within declaration");
      THROW(e);
    }

    while (def->which == IMPORT) {
      e = scope_lookup_import_globalenv(&def, mod, def, false);
      EXCEPT(e);
    }
    modbody = node_module_owner_const(def)->body;

    if (def->which != DEFNAME || !def->as.DEFNAME.is_globalenv) {
      e = mk_except(mod, node, "invalid within expression,"
                    " must point to a globalenv declaration");
      THROW(e);
    }

    if (mod->state->top_state == NULL) {
      // We're in a toplevel within, attach the topdeps to any suitable
      // toplevel node. If none is found, then the globalenv referenced by
      // this within is not used.
      FOREACH_SUB(top, mod->body) {
        if (NM(top->which) & (NM(DEFFUN) | NM(DEFMETHOD))) {
          PUSH_STATE(mod->state->top_state);
          mod->state->top_state->top = top;
          mod->state->top_state->exportable = top;
          topdeps_record_dyn(mod, def->typ);
          topdeps_record_global(mod, def);
          POP_STATE(mod->state->top_state);
          break;
        }
      }
    } else {
      topdeps_record_dyn(mod, def->typ);
      topdeps_record_global(mod, def);
    }

    node->as.WITHIN.globalenv_scoper = modbody->as.MODULE_BODY.globalenv_scoper;
  } else if (node->which == IDENT) {
    e = scope_lookup(&def, mod, node, node, false);
    EXCEPT(e);
    node->as.IDENT.def = def;
  } else {
    goto malformed;
  }

  set_typ(&node->typ, def->typ);
  node->flags |= def->flags & NODE__TRANSITIVE;
  return 0;

malformed:
  e = mk_except(mod, node, "malformed within expression");
  THROW(e);
}

static ERROR type_inference_try(struct module *mod, struct node *node) {
  struct node *eblock = subs_last(node);

  struct typ *u = NULL;
  FOREACH_SUB(b, eblock) {
    if (u == NULL) {
      u = b->typ;
    } else {
      error e = unify(mod, b, b->typ, u);
      EXCEPT(e);
    }
  }

  set_typ(&node->typ, u);

  return 0;
}

static enum token_type refop_for_typ(struct typ *r) {
  if (!typ_is_reference(r)) {
    return 0;
  }
  struct typ *r0 = typ_generic_functor(r);
  if (r0 == NULL) {
    return 0;
  }

  if (r0 == TBI_REF) {
    return TREFDOT;
  } else if (r0 == TBI_MREF) {
    return TREFBANG;
  } else if (r0 == TBI_MMREF) {
    return TREFSHARP;
  } else if (r0 == TBI_NREF) {
    return TNULREFDOT;
  } else if (r0 == TBI_NMREF) {
    return TNULREFBANG;
  } else if (r0 == TBI_NMMREF) {
    return TNULREFSHARP;
  } else if (r0 == TBI_CREF) {
    return TCREFDOT;
  } else if (r0 == TBI_CMREF) {
    return TCREFBANG;
  } else if (r0 == TBI_CMMREF) {
    return TCREFSHARP;
  } else if (r0 == TBI_CNREF) {
    return TNULCREFDOT;
  } else if (r0 == TBI_CNMREF) {
    return TNULCREFBANG;
  } else if (r0 == TBI_CNMMREF) {
    return TNULCREFSHARP;
  }
  return 0;
}

static ERROR type_inference_typeconstraint_defchoice_init(struct module *mod,
                                                          struct node *node) {
  error e;
  struct node *left = subs_first(node);
  struct node *right = subs_last(node);

  assert(left->which == INIT);
  assert(typ_definition_which(left->typ) == DEFINCOMPLETE);

  assert(right->flags & NODE_IS_DEFCHOICE);
  assert(right->which == BIN);
  struct tit *leaf = typ_definition_one_member(subs_first(right)->typ, node_ident(subs_last(right)));
  assert(tit_which(leaf) == DEFCHOICE);
  if (!tit_defchoice_is_leaf(leaf)) {
    e = mk_except_type(mod, subs_last(right),
                       "only union leaf variants can be initialized");
    THROW(e);
  }

  // With an initializer, 'un.A' is of type 'un', unlike for an accessor
  // (with external payload). So here we fix the typing of 'right' as bin
  // acc.
  unset_typ(&right->typ);
  set_typ(&right->typ, subs_first(right)->typ);

  left->as.INIT.for_tag = tit_ident(leaf);

  struct node *nxt = subs_first(left);
  while (nxt != NULL) {
    struct node *name = nxt;
    nxt = next(nxt);
    struct node *value = nxt;

    struct tit *f = tit_defchoice_lookup_field(leaf, node_ident(name));
    if (f == NULL) {
      e = mk_except_type(mod, name, "field '%s' not found",
                         idents_value(mod->gctx, node_ident(name)));
      THROW(e);
    }

    struct typ *target = tit_typ(f);
    const enum token_type explicit_ref = refop_for_typ(target);

    bool did_ref = false;
    e = try_insert_automagic_arg(&did_ref, &value, mod, node, target, explicit_ref);
    EXCEPT(e);

    e = try_insert_automagic_arg_de(&value, mod, target, explicit_ref);
    EXCEPT(e);

    if (!did_ref) {
      e = try_insert_automagic_arg(&did_ref, &value, mod, node, target, explicit_ref);
      EXCEPT(e);
    }

    value = next(name);

    e = unify(mod, value, tit_typ(f), value->typ);
    EXCEPT(e);

    tit_next(f);

    if (nxt != NULL) {
      nxt = next(nxt);
    }
  }

  tit_next(leaf);
  return 0;
}

static ERROR type_inference_typeconstraint_defchoice_convert(struct module *mod,
                                                             struct node *node) {
  error e;
  struct node *left = subs_first(node);
  struct node *right = subs_last(node);

  assert(right->which == BIN);
  struct tit *leaf = typ_definition_one_member(subs_first(right)->typ, node_ident(subs_last(right)));
  assert(tit_which(leaf) == DEFCHOICE);
  if (!tit_defchoice_is_leaf(leaf)
      || !(right->flags & NODE_IS_DEFCHOICE_HAS_EXTERNAL_PAYLOAD)) {
    e = mk_except_type(mod, subs_last(right),
                       "only union leaf variants with external payload"
                       " can constraint a value");
    THROW(e);
  }

  e = unify(mod, subs_first(node),
            left->typ, right->typ);
  EXCEPT(e);

  node_set_which(node, INIT);
  node->as.INIT.is_defchoice_external_payload_constraint = true;
  node->as.INIT.for_tag = tit_ident(leaf);

  node_subs_remove(node, right);
  set_typ(&node->typ, subs_first(right)->typ);
  node->flags |= subs_last(node)->flags;

  const struct node *except[] = { left, NULL };
  e = catchup(mod, except, node, CATCHUP_REWRITING_CURRENT);
  EXCEPT(e);

  return 0;
}

static ERROR type_inference_typeconstraint(struct module *mod, struct node *node) {
  if (node->as.TYPECONSTRAINT.is_constraint) {
    set_typ(&node->typ, subs_first(node)->typ);
    return 0;
  }

  error e;
  if (subs_first(node)->which == INIT
      && subs_last(node)->flags & NODE_IS_DEFCHOICE) {
    e = type_inference_typeconstraint_defchoice_init(mod, node);
    EXCEPT(e);
  } else if (subs_last(node)->flags & NODE_IS_DEFCHOICE) {
    e = type_inference_typeconstraint_defchoice_convert(mod, node);
    EXCEPT(e);
    return 0;
  }

  set_typ(&node->typ, subs_first(node)->typ);
  e = unify(mod, subs_first(node),
            subs_first(node)->typ, subs_last(node)->typ);
  EXCEPT(e);

  node->flags |= subs_first(node)->flags;
  node->flags |= subs_last(node)->flags & NODE__TYPECONSTRAINT_TRANSITIVE;
  // Copy flags back, as TYPECONSTRAINT are elided in
  // step_remove_typeconstraints().
  subs_first(node)->flags |= node->flags;

  return 0;
}

static ERROR check_void_body(struct module *mod, const struct node *node) {
  if (!node_has_tail_block(node)) {
    return 0;
  }
  const struct node *body = subs_last_const(node);
  if (body->typ != NULL && !typ_equal(body->typ, TBI_VOID)) {
    error e = mk_except_type(mod, body,
                             "the body of a function must be a block of type"
                             " Void (tip: use return), not '%s'",
                             pptyp(mod, body->typ));
    THROW(e);
  }
  return 0;
}

STEP_NM(step_type_inference,
        -1);
error step_type_inference(struct module *mod, struct node *node,
                          void *user, bool *stop) {
  DSTEP(mod, node);
  error e;

  if (node->typ == TBI__NOT_TYPEABLE) {
    return 0;
  }

  switch (node->which) {
  case DEFFUN:
  case DEFMETHOD:
    e = check_void_body(mod, node);
    EXCEPT(e);
    // Fallthrough
  case DEFINTF:
  case DEFTYPE:
  case DEFINCOMPLETE:
    assert(node->typ != NULL);
    // Already typed.
    topdeps_record(mod, node->typ);
    return 0;
  case IMPORT:
    if (node->typ != NULL) {
      // Already typed.
      topdeps_record(mod, node->typ);
      return 0;
    }
    break;
  default:
    break;
  }

  if (node->typ == NULL
      || node->typ == TBI__MUTABLE
      || node->typ == TBI__MERCURIAL
      || node->typ == TBI__CALL_FUNCTION_SLOT
      || node->which == DEFNAME
      || typ_definition_which(node->typ) == MODULE
      || typ_definition_which(node->typ) == ROOT_OF_ALL) {
    // noop
  } else {
    return 0;
  }

  BEGTIMEIT(TIMEIT_TYPE_INFERENCE);
  BEGTIMEIT(TIMEIT_TYPE_INFERENCE_PREBODYPASS);
  BEGTIMEIT(TIMEIT_TYPE_INFERENCE_IN_FUNS_BLOCK);

  switch (node->which) {
  case NIL:
    set_typ(&node->typ, create_tentative(mod, node, TBI_LITERALS_NIL));
    break;
  case IDENT:
    e = type_inference_ident(mod, node);
    EXCEPT(e);
    break;
  case DEFNAME:
    PUSH_STATE(node->as.DEFNAME.phi_state);

    set_typ(&node->typ, subs_last(node)->typ);
    node->flags |= subs_last(node)->flags & NODE__ASSIGN_TRANSITIVE;
    node->flags |= parent_const(node)->flags & NODE_IS_GLOBAL_LET;

    if (try_remove_unnecessary_ssa_defname(mod, node)) {
      set_typ(&node->typ, TBI_VOID);
      break;
    }

    if (node->flags & NODE_IS_TYPE) {
      e = mk_except(mod, node, "let cannot be used with a type name (use alias)");
      THROW(e);
    }

    if (typ_equal(node->typ, TBI_VOID)) {
      e = mk_except(mod, node, "cannot define a variable of type 'void'");
      THROW(e);
    }
    break;
  case DEFALIAS:
    if (nparent(node, 2)->which == DEFINTF) {
      set_typ(&node->typ, typ_create_ungenarg(subs_last(node)->typ));
    } else {
      set_typ(&node->typ, subs_last(node)->typ);
    }
    node->flags |= subs_last(node)->flags & NODE__TRANSITIVE;
    if (!(node->flags & NODE_IS_TYPE)) {
      e = mk_except(mod, node, "alias cannot be used with a value (use let)");
      THROW(e);
    }
    break;
  case PHI:
    node->typ = TBI__NOT_TYPEABLE;
    break;
  case NUMBER:
    set_typ(&node->typ, create_tentative(mod, node, number_literal_typ(mod, node)));
    break;
  case BOOL:
    set_typ(&node->typ, TBI_BOOL);
    break;
  case STRING:
    set_typ(&node->typ, create_tentative(mod, node, TBI_LITERALS_STRING));
    break;
  case SIZEOF:
    set_typ(&node->typ, TBI_UINT);
    break;
  case ALIGNOF:
    set_typ(&node->typ, TBI_UINT);
    break;
  case BIN:
    e = type_inference_bin(mod, node);
    EXCEPT(e);
    break;
  case UN:
    e = type_inference_un(mod, node);
    EXCEPT(e);
    break;
  case TUPLE:
    e = type_inference_tuple(mod, node);
    EXCEPT(e);
    break;
  case CALLNAMEDARG:
    set_typ(&node->typ, subs_first(node)->typ);
    break;
  case CALL:
    e = type_inference_call(mod, node);
    EXCEPT(e);
    break;
  case INIT:
    e = type_inference_init(mod, node);
    EXCEPT(e);
    break;
  case RETURN:
    e = type_inference_return(mod, node);
    EXCEPT(e);
    break;
  case BLOCK:
    e = type_inference_block(mod, node);
    EXCEPT(e);
    break;
  case CATCH:
    set_typ(&node->typ, subs_last(node)->typ);
    break;
  case THROW:
    {
      struct node *tryy = module_excepts_get(mod)->tryy;
      struct node *err = subs_at(subs_first(subs_first(tryy)), 1);
      assert(err->which == DEFNAME);
      e = unify(mod, node, subs_last(node)->typ, err->typ);
      EXCEPT(e);
      set_typ(&node->typ, TBI_VOID);
      break;
    }
  case JUMP:
  case BREAK:
  case CONTINUE:
  case NOOP:
    set_typ(&node->typ, TBI_VOID);
    break;
  case IF:
    e = type_inference_if(mod, node);
    EXCEPT(e);
    break;
  case WHILE:
    set_typ(&node->typ, TBI_VOID);
    struct node *cond = subs_first(node);
    e = unify_intf(mod, cond, cond->typ, TBI_GENERALIZED_BOOLEAN);
    EXCEPT(e);
    struct node *block = subs_at(node, 1);
    e = typ_check_equal(mod, block, block->typ, TBI_VOID);
    EXCEPT(e);
    break;
  case TRY:
    e = type_inference_try(mod, node);
    EXCEPT(e);
    break;
  case CONV:
    assert(typ_is_reference(subs_first(node)->typ));
    set_typ(&node->typ, node->as.CONV.to);
    break;
  case TYPECONSTRAINT:
    e = type_inference_typeconstraint(mod, node);
    EXCEPT(e);
    break;
  case DEFARG:
    PUSH_STATE(node->as.DEFARG.phi_state);

    // Necessary so we can recognize this IDENT as pointing to an arg.
    assert(subs_first(node)->which == IDENT);
    subs_first(node)->as.IDENT.def = node;

    set_typ(&node->typ, subs_at(node, 1)->typ);
    if (node->as.DEFARG.is_optional) {
      if (!typ_is_nullable_reference(node->typ) && !typ_is_optional(node->typ)) {
        e = mk_except_type(mod, node, "optional argument '%s' must be an optional"
                           " value or a nullable reference, not '%s'",
                           idents_value(mod->gctx, node_ident(node)),
                           pptyp(mod, node->typ));
        THROW(e);
      }
    } else if (node->as.DEFARG.is_vararg) {
      if (!typ_has_same_generic_functor(mod, node->typ, TBI_VARARG)) {
        e = mk_except_type(mod, node,
                           "vararg argument '%s' must have type"
                           " (vararg `any_any_ref), not '%s'",
                           idents_value(mod->gctx, node_ident(node)),
                           pptyp(mod, node->typ));
        THROW(e);
      }
    }
    break;
  case DEFGENARG:
    node->typ = typ_create_ungenarg(subs_at(node, 1)->typ);
    node->flags |= NODE_IS_TYPE;
    node_toplevel(nparent(node, 2))->flags |= TOP_IS_FUNCTOR;
    break;
  case SETGENARG:
    set_typ(&node->typ, subs_at(node, 1)->typ);
    node->flags |= NODE_IS_TYPE;
    node_toplevel(nparent(node, 2))->flags &= ~TOP_IS_FUNCTOR;
    break;
  case DEFFIELD:
    set_typ(&node->typ, subs_at(node, 1)->typ);
    break;
  case LET:
    set_typ(&node->typ, TBI_VOID);
    break;
  case ASSERT:
  case PRE:
  case POST:
  case INVARIANT:
    set_typ(&node->typ, TBI_VOID);
    break;
  case WITHIN:
    if (subs_count_atleast(node, 1) && subs_first(node)->which != WITHIN) {
      e = type_inference_within(mod, node);
      EXCEPT(e);
    } else {
      set_typ(&node->typ, TBI_VOID);
    }
    break;
  case ISALIST:
  case GENARGS:
  case FUNARGS:
    node->typ = TBI__NOT_TYPEABLE;
    break;
  case IMPORT:
    node->typ = TBI__NOT_TYPEABLE;
    node->flags = NODE_IS_TYPE;
    break;
  case ISA:
    set_typ(&node->typ, subs_first(node)->typ);
    node->flags = subs_first(node)->flags & NODE__TRANSITIVE;
    break;
  case DIRECTDEF:
    set_typ(&node->typ, node->as.DIRECTDEF.typ);
    node->flags = node->as.DIRECTDEF.flags;
    break;
  case DEFCHOICE:
    {
      const struct node *par = node;
      do {
        par = parent_const(par);
      } while (par->which == DEFCHOICE);
      set_typ(&node->typ, par->typ);
    }
    break;
  case MODULE_BODY:
    node->typ = TBI__NOT_TYPEABLE;
    break;
  default:
    break;
  }

  if (node->typ != NULL) {
    topdeps_record(mod, node->typ);
  }

  ENDTIMEIT(mod->state->fun_state != NULL && mod->state->fun_state->in_block,
            TIMEIT_TYPE_INFERENCE_IN_FUNS_BLOCK);
  ENDTIMEIT(mod->stage->state->passing < PASSZERO_COUNT + PASSFWD_COUNT,
            TIMEIT_TYPE_INFERENCE_PREBODYPASS);
  ENDTIMEIT(true, TIMEIT_TYPE_INFERENCE);

  assert(node->typ != NULL
         || (node->which == IDENT
             && "tolerate when it's a CATCH label or WITHIN label"));
  return 0;
}

STEP_NM(step_type_drop_excepts,
        NM(TRY));
error step_type_drop_excepts(struct module *mod, struct node *node,
                             void *user, bool *stop) {
  DSTEP(mod, node);

  module_excepts_close_try(mod);

  return 0;
}

static ERROR finalize_generic_instantiation(struct typ *t) {
  struct module *mod = typ_module_owner(t);

  if (typ_was_zeroed(t)) {
    // 't' was cleared in link_to_final()
    return 0;
  }

  if (typ_generic_arity(t) == 0) {
    // For instance, a DEFINCOMPLETE that unified to a non-generic.
    return 0;
  }

  if (typ_is_pseudo_builtin(t)) {
    return 0;
  }

  struct typ *functor = typ_generic_functor(t);

  if (typ_is_tentative(t)) {
    assert(!typ_is_tentative(functor));

    for (size_t m = 0; m < typ_generic_arity(t); ++m) {
      struct typ *arg = typ_generic_arg(t, m);
      assert(!typ_is_tentative(arg));
    }
  } else {
    // While t is already not tentative anymore, the instantiation is still
    // partial, and needs to be completed (bodypass). It's easier to simply
    // create a final instance from scratch.
  }

  const size_t arity = typ_generic_arity(t);

  struct typ *existing = instances_find_existing_final_like(t);
  if (existing != NULL) {
    typ_link_to_existing_final(existing, t);
    topdeps_record(mod, existing);
    return 0;
  }

  struct typ **args = calloc(arity, sizeof(struct typ *));
  for (size_t m = 0; m < arity; ++m) {
    args[m] = typ_generic_arg(t, m);
  }

  struct typ *i = NULL;
  error e = instantiate(&i, mod, typ_for_error(t), -1, functor, args, arity,
                        true);
  EXCEPT(e);

  assert(!typ_is_tentative(i));
  if (!typ_was_zeroed(t)) {
    // Otherwise it's already linked.
    typ_link_to_existing_final(i, t);
  }

  topdeps_record(mod, i);

  free(args);
  return 0;
}

// Typically, unify_with_defincomplete() finishes the job. But in some cases
// it's not enough.
//   fun foo = error
//     let e = {}:(definc {})
//     let a = {x=0}:(definc {x:`integer})
//     e:(definc {x:`integer}) = a -- a.typ is linked to e.typ
//     return e
// The typing of the return unifies 'e.typ' to 'error', which correctly
// updates 'a.typ' to 'error', but unify_with_defincomplete() only sees
// 'error' and 'e.typ', and doesn't know to find the definition 'a.typ' and
// unify the fields of *that* DEFINCOMPLETE (it does know about the
// different DEFINCOMPLETE behind 'e.typ').
//
// So we fix it after the fact.
static void finalize_defincomplete_unification(struct typ *inc) {
  error e = unify_with_defincomplete_entrails(typ_module_owner(inc),
                                              typ_for_error(inc), inc, inc);
  assert(!e);
}

static __thread struct vectyp scheduleq;

// Called from typ_link_*() operations. Linking is triggered by unify*().
// unify*() works directly on typs. When a typ is linked, the source is
// zeroed. In some cases (currently, on link_finalize()), we have to wait
// until after unify*() is done to proceed.
//
// Why only link_finalize()?
// unify*() respects this invariant: during unification, a tentative typ may
// be linked to another tentative typ or final typ; once *explicitly* linked
// to something else (explicitly means passed directly to typ_link_*()) by
// unify*()), tentative typs are not used again by the current unification.
// So there is no need to delay further work, they can be linked and zeroed
// immediately.
//
// link_finalize(), on the other hand, operates on tentative typs that have
// just lost their tentative status but were not directly passed to
// typ_link_*(). They lost their tentative status as a side effect of some
// other typ being linked to something final. The current unification
// invokation has no way of knowing all the typs that may loose their
// tentative status indirectly. If link_finalize() were to do the link
// immediately, unify*() would end up using zeroed typs by mistake. Instead,
// the work is delayed until the current unification is over.
void schedule_finalization(struct typ *t) {
  vectyp_push(&scheduleq, t);
}

error process_finalizations(void) {
  static __thread bool in_progress;
  if (in_progress) {
    return 0;
  }
  in_progress = true;

  // More work may be scheduled as we're iterating.
  for (size_t n = 0; n < vectyp_count(&scheduleq); ++n) {
    struct typ *t = *vectyp_get(&scheduleq, n);
    if (typ_was_zeroed(t)) {
      continue;
    }

    if (typ_definition_which(t) == DEFINCOMPLETE) {
      finalize_defincomplete_unification(t);
    } else {
      error e = finalize_generic_instantiation(t);
      EXCEPT(e);
    }
  }
  vectyp_destroy(&scheduleq);

  in_progress = false;
  return 0;
}
