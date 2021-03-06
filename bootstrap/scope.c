#include "scope.h"

#include "table.h"
#include "types.h"
#include "nodes.h"
#include "parser.h"

struct node *scope_node(struct scope *sc) {
  return sc->node;
}

const struct node *scope_node_const(const struct scope *sc) {
  return scope_node(CONST_CAST(sc));
}

static const struct scope *scope_parent(const struct scope *sc) {
  const struct node *par = parent_const(scope_node_const(sc));
  while (par != NULL && par->__scope == NULL) {
    par = parent_const(par);
  }
  return par == NULL ? NULL : par->__scope;
}

uint32_t ident_hash(const ident *a) {
  return *a * 31;
}

int ident_cmp(const ident *a, const ident *b) {
  const ident aa = *a;
  const ident bb = *b;
  return aa == bb ? 0 : (aa > bb ? 1 : -1);
}

IMPLEMENT_HTABLE_SPARSE(unused__ static, scope_map, struct node *, ident,
                        ident_hash, ident_cmp);

void scope_init(struct node *scoper) {
  scope_destroy(scoper);
  assert(scoper->__scope == NULL);
  scoper->__scope = calloc(1, sizeof(struct scope));
  scoper->__scope->node = scoper;
  scope_map_init(&scoper->__scope->map, 0);
  scope_map_set_delete_val(&scoper->__scope->map, NULL);
}

void scope_destroy(struct node *scoper) {
  if (scoper->__scope != NULL) {
    scope_map_destroy(&scoper->__scope->map);
    scoper->__scope->node = NULL;
    free(scoper->__scope);
    scoper->__scope = NULL;
  }
}

// Return value must be freed by caller.
char *scope_name(const struct module *mod, const struct node *scoper) {
  if (parent_const(scoper) == NULL) {
    const char *name = idents_value(mod->gctx, node_ident(scoper));
    char *r = calloc(strlen(name) + 1, sizeof(char));
    strcpy(r, name);
    return r;
  }

  ssize_t len = 0;
  const struct node *root = scoper;
  while (parent_const(root) != NULL) {
    ident id = node_ident(root);
    if (id != ID_ANONYMOUS) {
      const char *name = idents_value(mod->gctx, id);
      size_t name_len = strlen(name);
      len += name_len + 1;
    }
    root = parent_const(root);
  }
  len = max(size_t, 1, len) - 1;

  char *r = calloc(len + 1, sizeof(char));
  root = scoper;
  while (parent_const(root) != NULL) {
    ident id = node_ident(root);
    if (id != ID_ANONYMOUS) {
      const char *name = idents_value(mod->gctx, id);
      size_t name_len = strlen(name);
      if (parent_const(parent_const(root)) == NULL) {
        memcpy(r, name, name_len);
      } else {
        len -= name_len + 1;
        if (len < 0) {
          // Can happen if there are several anonymous nodes at the root.
          memcpy(r, name, name_len);
        } else {
          r[len] = '.';
          memcpy(r + len + 1, name, name_len);
        }
      }
    }
    root = parent_const(root);
  }

  return r;
}

struct scope_definitions_name_list_data {
  const struct module *mod;
  char *s;
  size_t len;
};

static int scope_definitions_name_list_iter(const ident *key,
                                            struct node **val,
                                            void *user) {
  struct scope_definitions_name_list_data *d = user;
  const char *n = idents_value(d->mod->gctx, *key);
  const size_t nlen = strlen(n);

  const bool first = d->s == NULL;

  d->s = realloc(d->s, d->len + nlen + (first ? 1 : 3));
  char *p = d->s + d->len;

  if (!first) {
    strcpy(p, ", ");
    p += 2;
    d->len += 2;
  }

  strcpy(p, n);
  d->len += nlen;

  return 0;
}

char *scope_definitions_name_list(const struct module *mod,
                                  const struct node *scoper) {
  if (scoper->__scope == NULL) {
    return strdup("");
  }

  struct scope_definitions_name_list_data d = {
    .mod = mod,
    .s = NULL,
    .len = 0,
  };

  scope_map_foreach((struct scope_map *)&scoper->__scope->map,
                    scope_definitions_name_list_iter, &d);

  return d.s;
}

size_t scope_count(const struct scope *scope) {
  return scope_map_count((struct scope_map *)&scope->map);
}

void scope_undefine_ssa_var(struct scope *scope, ident id) {
  assert(id != ID__NONE);
  struct node **existing = scope_map_get(&scope->map, id);
  assert(existing != NULL && *existing != NULL);
  assert((*existing)->which == DEFNAME && (*existing)->as.DEFNAME.ssa_user != NULL);
  *existing = NULL;
}

error scope_define_ident(const struct module *mod, struct node *scoper,
                         ident id, struct node *node) {
  if (scoper->__scope == NULL) {
    goto doesnt_exist;
  }

  struct scope *scope = node_scope(scoper);

  assert(id != ID__NONE);
  struct node **existing = scope_map_get(&scope->map, id);

  if (existing != NULL
      && (NM((*existing)->which) & (STEP_NM_DEFS | NM(DEFNAME) | NM(DEFALIAS)))
      && node->which == IMPORT) {
    // Local definition masks import.
    return 0;
  }

  // TODO: support for prototypes may go away, as they're not strictly
  // necessary with the arbitrary definition ordering handling. But we still
  // to allow replacing placeholder modules.

  // If existing is prototype, we just replace it with full definition.
  // If not, it's an error:
  if (existing != NULL
      && (!node_is_prototype(*existing)
          || node->which != (*existing)->which)) {
    const struct module *existing_mod = try_node_module_owner_const(mod, *existing);
    char *scname = scope_name(mod, scoper);
    error e = mk_except(try_node_module_owner_const(mod, node), node,
                        "in scope %s: identifier '%s' already defined at %s:%d:%d",
                        scname, idents_value(mod->gctx, id),
                        module_component_filename_at(existing_mod, (*existing)->codeloc.pos),
                        (*existing)->codeloc.line, (*existing)->codeloc.column);
    free(scname);
    THROW(e);
  } else if (existing != NULL) {
    struct toplevel *toplevel = node_toplevel(*existing);
    if (toplevel != NULL) {
      // FIXME? Should it only be possible to shadow an imported name from
      // an from xxx import *
      toplevel->flags |= TOP_IS_SHADOWED;
    }
    *existing = node;
  } else {
doesnt_exist:
    if (scoper->__scope == NULL || scoper->__scope->map.table.size == 0) {
      // Lazy initialize scope. It is a property of the underlying sptable
      // that when its size is 0, it has not be initialized.
      scope_init(scoper);
    }

    scope_map_set(&scoper->__scope->map, id, node);
  }

  return 0;
}

error scope_define(const struct module *mod, struct node *scoper,
                   struct node *id, struct node *node) {
  assert(id->which == IDENT);
  error e = scope_define_ident(mod, scoper, node_ident(id), node);
  EXCEPT(e);
  return 0;
}

struct use_isalist_state {
  struct node *result;
  const struct node *for_error;
  ident id;
  bool is_bin_acc;
};

static ERROR do_scope_lookup_ident_immediate(struct node **result,
                                             const struct node *for_error,
                                             const struct module *mod,
                                             const struct node *scoper, ident id,
                                             bool allow_isalist, bool failure_ok,
                                             bool is_bin_acc);

static ERROR use_isalist_scope_lookup(struct module *mod,
                                      struct typ *t, struct typ *intf,
                                      bool *stop, void *user) {
  struct use_isalist_state *st = user;

  struct node *result = NULL;
  error e = do_scope_lookup_ident_immediate(&result, st->for_error, mod,
                                            typ_definition_ignore_any_overlay_const(intf),
                                            st->id, false, true, st->is_bin_acc);

  if (!e) {
    if (result->which == DEFFUN || result->which == DEFMETHOD
        || result->which == DEFALIAS) {
      st->result = result;
      *stop = true;
    }
  }

  return 0;
}

static ERROR do_scope_lookup_ident_immediate(struct node **result,
                                             const struct node *for_error,
                                             const struct module *mod,
                                             const struct node *scoper, ident id,
                                             bool allow_isalist, bool failure_ok,
                                             bool is_bin_acc) {
  const struct scope *scope = node_scope(scoper);
  if (scope == NULL) {
    goto failed;
  }

  const bool use_isalist = allow_isalist
    && (NM(scoper->which) & (NM(DEFINTF) | NM(DEFINCOMPLETE)))
    && scoper->typ != NULL;

  assert(id != ID__NONE);
  struct node **r = scope_map_get((struct scope_map *)&scope->map, id);
  if (r != NULL) {
    if (!is_bin_acc) {
      if (NM((*r)->which) & (NM(DEFFIELD) | NM(DEFMETHOD) | NM(DEFFUN))) {
        if (NM(parent_const(*r)->which) & (NM(DEFTYPE) | NM(DEFINTF))) {
          r = NULL;
          goto not_found;
        }
      }
    }

    *result = *r;
    return 0;
  }

not_found:
  if (scoper->which == MODULE
      && subs_count_atleast(scoper, 1)) {
    const struct node *body = subs_first_const(scoper);
    error e = do_scope_lookup_ident_immediate(result, for_error, mod, body,
                                            id, allow_isalist, failure_ok,
                                              is_bin_acc);
    if (!e) {
      return 0;
    } else if (failure_ok) {
      return e;
    } else {
      EXCEPT(e);
    }
  }

  if (use_isalist) {
    const struct node *par = scoper;
    // FIXME: We should statically detect when this search would return
    // different results depending on order. And we should force the caller
    // to specificy which intf is being called if it's ambiguous.
    // FIXME: Should prevent lookups from another module to use non-exported
    // interfaces.
    enum isalist_filter filter = 0;
    struct use_isalist_state st = {
      .result = NULL,
      .for_error = for_error,
      .id = id,
      .is_bin_acc = is_bin_acc,
    };

    error e = typ_isalist_foreach((struct module *) mod, par->typ, filter,
                                  use_isalist_scope_lookup, &st);
    EXCEPT(e);

    if (st.result != NULL) {
      *result = st.result;
      return 0;
    }
  }

failed:
  if (failure_ok) {
    return EINVAL;
  } else {
    char *scname = scope_name(mod, scoper);
    char *escname = scope_name(mod, for_error);
    error e = mk_except(try_node_module_owner_const(mod, for_error), for_error,
                        "from scope %s: in scope %s: unknown identifier '%s'",
                        escname, scname, idents_value(mod->gctx, id));
    free(escname);
    free(scname);
    THROW(e);
  }

  return 0;
}

error scope_lookup_ident_immediate(struct node **result, const struct node *for_error,
                                   const struct module *mod,
                                   const struct node *scoper, ident id,
                                   bool failure_ok) {
  return do_scope_lookup_ident_immediate(result, for_error, mod, scoper, id,
                                         true, failure_ok, true);
}

static ERROR do_scope_lookup_ident_wontimport(struct node **result, const struct node *for_error,
                                              const struct module *mod,
                                              const struct node *scoper, ident id,
                                              bool failure_ok) {
  const struct scope *scope = node_scope(scoper);

  char *scname = NULL;
  char *escname = NULL;
  error e;

skip:
  e = do_scope_lookup_ident_immediate(result, for_error, mod, scoper, id, false, true, false);
  if (!e) {
    if (scoper->which == DEFTYPE
        && ((*result)->which == DEFFUN || (*result)->which == DEFMETHOD)) {
      // Skip scope: id is a bare identifier, cannot reference functions or
      // methods in the scope of a DEFTYPE. Must use the form 'this.name'.
      scope = scope_parent(scope);
      goto skip;
    }
    return 0;
  }

  // Will not go up in modules_root past a MODULE_BODY node as there is no
  // permission to access these scopes unless descending from
  // gctx->modules_root.
  if (scope_parent(scope) == NULL
      || (scope != NULL && scoper->which == MODULE_BODY)) {
    if (failure_ok) {
      return e;
    } else {
      scname = scope_name(mod, scoper);
      escname = scope_name(mod, for_error);
      e = mk_except(try_node_module_owner_const(mod, for_error), for_error,
                    "from scope %s: in scope %s: unknown identifier '%s'",
                    escname, scname, idents_value(mod->gctx, id));
      free(escname);
      free(scname);
      THROW(e);
    }
  }

  e = do_scope_lookup_ident_wontimport(result, for_error, mod, scope_node_const(scope_parent(scope)),
                                       id, failure_ok);
  return e;
}

error scope_lookup_ident_wontimport(struct node **result, const struct node *for_error,
                                    const struct module *mod,
                                    const struct node *scoper, ident id, bool failure_ok) {
  return do_scope_lookup_ident_wontimport(result, for_error, mod, scoper, id, failure_ok);
}

#define EXCEPT_UNLESS(e, failure_ok) do { \
  if (e) {\
    if (failure_ok) { \
      return e; \
    } else { \
      THROW(e); \
    } \
  } \
} while (0)

static ERROR do_scope_lookup(struct node **result, const struct node *for_error,
                             const struct module *mod,
                             const struct node *scoper, const struct node *id,
                             bool failure_ok, bool statement_rules) {
  error e;
  char *scname = NULL;
  char *escname = NULL;
  struct node *par = NULL, *r = NULL;

  switch (id->which) {
  case IDENT:
    e = do_scope_lookup_ident_wontimport(&r, for_error, mod, scoper,
                                         node_ident(id), failure_ok);
    EXCEPT_UNLESS(e, failure_ok);

    if (statement_rules
        && r->which == DEFNAME && !(r->flags & NODE_IS_GLOBAL_LET)
        && mod->state->fun_state != NULL && mod->state->fun_state->in_block) {
      if (r->as.DEFNAME.passed < mod->stage->state->passing) {
        // Definition not yet passed, so it's not in scope yet. Look in the
        // surrounding scope.
        if (r->as.DEFNAME.locally_shadowed == 0) {
          r->as.DEFNAME.locally_shadowed = 1 + mod->next_locally_shadowed;
          // Coming from scope_statement_lookup, where 'mod' is not const.
          CONST_CAST(mod)->next_locally_shadowed += 1;
        }

        struct node *r_statement = node_statement_owner(r);
        if (r_statement != NULL) {
          struct node *block = parent(r_statement);
          while (block->which == BLOCK && block->as.BLOCK.is_scopeless) {
            block = parent(block);
          }

          e = do_scope_lookup(&r, for_error, mod,
                              parent(block), id,
                              failure_ok, statement_rules);
          EXCEPT_UNLESS(e, failure_ok);
        }
      }
    }

    break;
  case BIN:
    if (id->as.BIN.operator != TDOT
        && id->as.BIN.operator != TBANG
        && id->as.BIN.operator != TSHARP) {
      GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, id), id, "malformed name");
    }
    const struct node *base = subs_first_const(id);
    const struct node *name = subs_last_const(id);
    if (name->which != IDENT) {
      GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, id), id, "malformed name");
    }

    e = do_scope_lookup(&par, for_error, mod, scoper, base, failure_ok, statement_rules);
    EXCEPT_UNLESS(e, failure_ok);

    e = do_scope_lookup_ident_immediate(&r, for_error, mod, par,
                                        node_ident(name), false, failure_ok,
                                        true);
    EXCEPT_UNLESS(e, failure_ok);

    break;
  case DIRECTDEF:
    r = typ_definition_ignore_any_overlay(id->as.DIRECTDEF.typ);
    break;
  default:
    assert(false);
    return 0;
  }

  if (r->which == IMPORT) {
    e = do_scope_lookup(&r, for_error, mod, &mod->gctx->modules_root,
                        subs_first_const(r), failure_ok, false);
    EXCEPT_UNLESS(e, failure_ok);
  }

  *result = r;
  return 0;

except:
  free(escname);
  free(scname);
  return e;
}

ERROR scope_lookup_import_globalenv(struct node **result, const struct module *mod,
                                    const struct node *import, bool failure_ok) {
  const struct node *path = subs_first_const(import);
  assert(path->which == BIN);
  error e = do_scope_lookup(result, import, mod, &mod->gctx->modules_root,
                            subs_first_const(path), failure_ok, false);
  EXCEPT(e);

  assert((*result)->which == MODULE);
  e = scope_lookup_ident_immediate(result, import, mod,
                                   (*result)->as.MODULE.mod->body->as.MODULE_BODY.globalenv_scoper,
                                   node_ident(subs_last_const(path)), failure_ok);
  EXCEPT(e);
  assert((*result)->which == DEFNAME && (*result)->as.DEFNAME.is_globalenv);
  return 0;
}

error scope_lookup(struct node **result, const struct module *mod,
                   const struct node *scoper, const struct node *id,
                   bool failure_ok) {
  return do_scope_lookup(result, id, mod, scoper, id, failure_ok, false);
}

error scope_statement_lookup(struct node **result, struct module *mod,
                             const struct node *scoper, const struct node *id,
                             bool failure_ok) {
  return do_scope_lookup(result, id, mod, scoper, id, failure_ok, true);
}

error scope_lookup_module(struct node **result, const struct module *mod,
                          const struct node *id, bool failure_ok) {
  error e = 0;
  struct node *par = NULL, *r = NULL;
  struct node *scoper = &mod->gctx->modules_root;
  const struct node *for_error = id;

  switch (id->which) {
  case IDENT:
    e = scope_lookup_ident_immediate(&r, for_error, mod, scoper,
                                     node_ident(id), true);
    break;
  case BIN:
    if (id->as.BIN.operator != TDOT) {
      e = mk_except_type(try_node_module_owner_const(mod, id), id,
                         "malformed module path name");
      THROW(e);
    }
    const struct node *base = subs_first_const(id);
    const struct node *name = subs_last_const(id);
    e = do_scope_lookup(&par, for_error, mod, scoper, base, true, false);
    if (e) {
      break;
    }
    e = do_scope_lookup(&r, for_error, mod, par, name, true, false);
    if (e) {
      break;
    }
    break;
  default:
    assert(false);
    return 0;
  }

  if (e || r->which != MODULE) {
    goto err;
  }

  *result = r;
  return 0;

err:
  if (failure_ok) {
    return EINVAL;
  } else {
    if (r != NULL && r->which != MODULE) {
      e = mk_except(try_node_module_owner_const(mod, id), id,
                    "not the name of a module");
      THROW(e);
    } else {
      e = mk_except(try_node_module_owner_const(mod, id), id,
                    "module not found");
      THROW(e);
    }
  }

  assert(false && "Unreached.");
  return 0;
}

struct scope_foreach_state {
  struct module *mod;
  scope_each each;
  void *user;
  error e;
};

static int scope_foreach_each(const ident *key, struct node **val, void *user) {
  if (*val == NULL) {
    return 0;
  }

  struct scope_foreach_state *st = user;
  st->e = st->each(st->mod, *val, st->user);
  return st->e ? 1 : 0;
}

error scope_foreach(struct module *mod, struct scope *scope,
                    scope_each each, void *user) {
  if (scope == NULL) {
    return 0;
  }

  struct scope_foreach_state st = {
    .mod = mod,
    .each = each,
    .user = user,
    .e = 0,
  };
  int ret = scope_map_foreach(&scope->map, scope_foreach_each, &st);
  if (ret) {
    THROW(st.e);
  }

  return 0;
}
