#include "unify.h"

#include "table.h"
#include "types.h"
#include "scope.h"
#include "parser.h"
#include "passes.h"
#include "instantiate.h"
#include "inference.h"

enum unify_flags {
  REFCOMPAT_LEFT = 0x1,
  REFCOMPAT_RIGHT = 0x2,
  REFCOMPAT__SWAPPABLES = REFCOMPAT_LEFT | REFCOMPAT_RIGHT,
};

#define SWAP_FLAGS(flags) do { \
  uint32_t tmp = (flags); \
  flags &= ~REFCOMPAT__SWAPPABLES; \
  if (tmp & REFCOMPAT_LEFT) { \
    flags |= REFCOMPAT_RIGHT; \
  } \
  if (tmp & REFCOMPAT_RIGHT) { \
    flags |= REFCOMPAT_LEFT; \
  } \
} while (0)

static ERROR do_unify(struct module *mod, uint32_t flags,
                      const struct node *for_error,
                      struct typ *a, struct typ *b);

static ERROR mk_except_type_unification(struct module *mod, const struct node *for_error,
                                        const struct typ *a, const struct typ *b) {
  char *sa = pptyp(mod, a);
  char *sb = pptyp(mod, b);
  error e = mk_except_type(mod, for_error,
                           "types '%s' and '%s' cannot be unified", sa, sb);
  free(sb);
  free(sa);
  THROW(e);
}

static ERROR unify_two_non_generic(struct module *mod, const struct node *for_error,
                                   struct typ *a, struct typ *b) {
  if (typ_isa(a, b)) {
    // noop
  } else if (typ_isa(b, a)) {
    SWAP(a, b);
  } else if (typ_definition_which(a) == DEFINTF
             && typ_definition_which(b) == DEFINTF) {
    struct node *dinc = defincomplete_create(mod, for_error);

    defincomplete_add_isa(mod, for_error, dinc, a);
    defincomplete_add_isa(mod, for_error, dinc, b);

    error e = defincomplete_catchup(mod, dinc);
    assert(!e);

    typ_link_tentative(dinc->typ, a);
    typ_link_tentative(dinc->typ, b);
  } else {
    error e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  typ_link_tentative(a, b);

  return 0;
}

static bool same_generic_functor(const struct module *mod,
                                 const struct typ *a, const struct typ *b) {
  if (typ_generic_arity(a) > 0 && typ_generic_arity(b) > 0) {
    return typ_equal(typ_generic_functor_const(a), typ_generic_functor_const(b));
  } else {
    return false;
  }
}

static ERROR unify_with_equal(struct module *mod, const struct node *for_error,
                              struct typ *a, struct typ *b);

static ERROR unify_same_generic_functor(struct module *mod, uint32_t flags,
                                        const struct node *for_error,
                                        struct typ *a, struct typ *b) {
  assert(typ_generic_arity(a) != 0 && typ_generic_arity(b) != 0);

  if (typ_equal(a, b)) {
    error e = unify_with_equal(mod, for_error, a, b);
    EXCEPT(e);
    return 0;
  }

  assert(typ_generic_arity(a) == typ_generic_arity(b));
  const size_t arity = typ_generic_arity(a);
  for (size_t n = 0; n < arity; ++n) {
    struct typ *arga = typ_generic_arg(a, n);
    struct typ *argb = typ_generic_arg(b, n);

    error e = do_unify(mod, flags, for_error, arga, argb);
    if (e) {
      e = mk_except_type(mod, for_error,
                         "unifying generic argument at position %zu", 1 + n);
      THROW(e);
    }
  }

  return 0;
}

struct instance_of {
  struct typ *functor;
  struct typ *result;
};

static ERROR find_instance_of(struct module *mod, struct typ *t,
                              struct typ *intf, bool *stop, void *user) {
  struct instance_of *r = user;

  struct typ *intf0 = typ_generic_functor(intf);
  if (intf0 != NULL && typ_equal(intf0, r->functor)) {
    r->result = intf;
    *stop = true;
  }

  return 0;
}

static ERROR unify_generics(struct module *mod, uint32_t flags,
                            const struct node *for_error,
                            struct typ *a, struct typ *b,
                            bool a_tentative, bool b_tentative) {
  struct typ *a0 = typ_generic_functor(a);
  struct typ *b0 = typ_generic_functor(b);
  bool a_genf = typ_is_generic_functor(a);
  bool b_genf = typ_is_generic_functor(b);

  error e;
  if (typ_isa(a0, b0)) {
    // noop
  } else if (a_tentative && typ_isa(b0, a0)) {
    SWAP(a, b);
    SWAP(a0, b0);
    SWAP(a_genf, b_genf);
    SWAP(a_tentative, b_tentative);
  } else if (typ_definition_which(a) == DEFINTF
             && typ_definition_which(b) == DEFINTF) {
    struct node *dinc = defincomplete_create(mod, for_error);
    defincomplete_add_isa(mod, for_error, dinc, a);
    defincomplete_add_isa(mod, for_error, dinc, b);
    e = defincomplete_catchup(mod, dinc);
    THROW(e);
  } else {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  if (!b_genf && !typ_equal(b, TBI_ANY)) {
    struct typ *b_in_a = NULL;

    if (typ_equal(a0, b0)) {
      b_in_a = a;
    } else {
      struct instance_of user = { .functor = b0, .result = NULL };
      error never = typ_isalist_foreach(mod, a, 0, find_instance_of, &user);
      assert(!never);
      assert(user.result != NULL);
      b_in_a = user.result;
    }

    assert(b_in_a != b && "FIXME What does that mean?");

    assert(!typ_is_tentative(a0) && "FIXME handle it");

    e = unify_same_generic_functor(mod, flags, for_error, b_in_a, b);
    EXCEPT(e);
  }

  // FIXME: there are holes here: 2nd order generics, etc.

  if (b_tentative) {
    typ_link_tentative(a, b);
  }

  return 0;
}

static ERROR unify_non_generic(struct module *mod, const struct node *for_error,
                               struct typ *a, struct typ *b,
                               bool a_non_generic, bool b_non_generic) {
  error e;

  if (a_non_generic && b_non_generic) {
    e = unify_two_non_generic(mod, for_error, a, b);
    EXCEPT(e);
    return 0;
  }

  if (!typ_is_tentative(b)) {
    SWAP(a, b);
  }
  if (!typ_is_tentative(b)) {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  e = typ_check_isa(mod, for_error, a, b);
  EXCEPT(e);

  if (typ_equal(a, b)) {
    e = unify_with_equal(mod, for_error, a, b);
    EXCEPT(e);
    return 0;
  }

  struct typ *b0 = typ_generic_functor(b);
  bool b_genf = typ_is_generic_functor(b);
  if (!b_genf) {
    struct typ *b_in_a = NULL;

    struct instance_of user = { .functor = b0, .result = NULL };
    error never = typ_isalist_foreach(mod, a, 0, find_instance_of, &user);
    assert(!never);
    assert(user.result != NULL);
    b_in_a = user.result;

    assert(b_in_a != b && "FIXME What does that mean?");

    e = unify_same_generic_functor(mod, 0, for_error, b_in_a, b);
    EXCEPT(e);
  }

  typ_link_tentative(a, b);

  return 0;
}

static struct typ *nullable(struct module *mod, const struct node *for_error,
                            struct typ *t) {
  if (typ_isa(t, TBI_ANY_NULLABLE_REF)) {
    return t;
  }

  struct typ *r0 = NULL;
  const struct typ *t0 = typ_generic_functor_const(t);
  if (typ_equal(t0, TBI_REF)) {
    r0 = TBI_NREF;
  } else if (typ_equal(t0, TBI_MREF)) {
    r0 = TBI_NMREF;
  } else if (typ_equal(t0, TBI_MMREF)) {
    r0 = TBI_NMMREF;
  } else {
    assert(false);
  }

  struct typ *a = typ_generic_arg(t, 0);

  struct node *i = NULL;
  error e = instantiate(&i, mod, for_error, 0, r0, &a, 1, false);
  assert(!e);
  return i->typ;
}

static struct typ *as_non_tentative(const struct typ *t) {
  return typ_as_non_tentative(t);;
}

static ERROR unify_literal(struct module *mod, uint32_t flags,
                           const struct node *for_error,
                           struct typ *a, struct typ *b,
                           bool a_literal, bool b_literal) {
  error e;
  if (a_literal && b_literal) {
    bool a_floating = typ_equal(a, TBI_LITERALS_FLOATING);
    bool b_floating = typ_equal(b, TBI_LITERALS_FLOATING);
    if (a_floating || b_floating) {
      if (!a_floating && b_floating) {
        SWAP(a, b);
        SWAP(a_floating, b_floating);
        SWAP_FLAGS(flags);
      }
      if (!a_floating) {
        if (!typ_equal(a, TBI_LITERALS_INTEGER)) {
          e = mk_except_type_unification(mod, for_error, a, b);
          THROW(e);
        }
      }
    } else {
      e = typ_check_equal(mod, for_error, a, b);
      EXCEPT(e);
    }

    typ_link_tentative(a, b);

    return 0;
  }

  if (a_literal) {
    SWAP(a, b);
  }

  if (typ_equal(b, TBI_LITERALS_NULL)) {
    if (typ_generic_functor(a) != NULL && (flags & REFCOMPAT_LEFT)) {
      e = do_unify(mod, flags & ~REFCOMPAT_LEFT, for_error, a, b);
      EXCEPT(e);
      return 0;
    } else if (flags & REFCOMPAT_RIGHT) {
      e = typ_check_isa(mod, for_error, a, TBI_ANY_REF);
      EXCEPT(e);

      typ_link_tentative(nullable(mod, for_error, a), b);
    } else {
      e = typ_check_isa(mod, for_error, a, TBI_ANY_NULLABLE_REF);
      EXCEPT(e);

      typ_link_tentative(a, b);
    }
  } else if (typ_equal(b, TBI_LITERALS_INTEGER)) {
    if (!typ_is_tentative(a) && typ_isa(TBI_INTEGER_LITERAL_COMPATIBLE, a)
        && !typ_equal(a, TBI_INTEGER_LITERAL_COMPATIBLE)) {
      // noop
    } else if (typ_is_tentative(a) && typ_isa(b, a)) {
      typ_link_tentative(b, a);
    } else if (!typ_isa(a, TBI_INTEGER_LITERAL_COMPATIBLE)
               && typ_is_tentative(a) && typ_definition_which(a) == DEFINTF) {
      struct node *dinc = defincomplete_create(mod, for_error);

      defincomplete_add_isa(mod, for_error, dinc, as_non_tentative(a));
      defincomplete_add_isa(mod, for_error, dinc, as_non_tentative(b));

      error e = defincomplete_catchup(mod, dinc);
      assert(!e);

      typ_link_tentative(dinc->typ, a);
      typ_link_tentative(dinc->typ, b);
    } else {
      e = typ_check_isa(mod, for_error, a, TBI_INTEGER_LITERAL_COMPATIBLE);
      EXCEPT(e);

      typ_link_tentative(a, b);
    }
  } else if (typ_equal(b, TBI_LITERALS_FLOATING) && !typ_equal(a, TBI_FLOATING)) {
    if (!typ_is_tentative(a) && typ_isa(TBI_FLOATING, a)) {
      // noop
    } else if (typ_is_tentative(a) && typ_isa(b, a)) {
      typ_link_tentative(b, a);
    } else if (!typ_isa(a, TBI_FLOATING)
               && typ_is_tentative(a) && typ_definition_which(a) == DEFINTF) {
      struct node *dinc = defincomplete_create(mod, for_error);

      defincomplete_add_isa(mod, for_error, dinc, a);
      defincomplete_add_isa(mod, for_error, dinc, b);

      error e = defincomplete_catchup(mod, dinc);
      assert(!e);

      typ_link_tentative(dinc->typ, a);
      typ_link_tentative(dinc->typ, b);
    } else {
      e = typ_check_isa(mod, for_error, a, TBI_FLOATING);
      EXCEPT(e);

      typ_link_tentative(a, b);
    }
  } else {
    assert(false);
  }

  return 0;
}

static ERROR unify_with_weakly_concrete(bool *success,
                                        struct module *mod,
                                        const struct node *for_error,
                                        struct typ *a, struct typ *b,
                                        bool a_weakly_concrete,
                                        bool b_weakly_concrete) {
  if (a_weakly_concrete && !b_weakly_concrete) {
    SWAP(a, b);
  }

  if (typ_equal(b, TBI_BOOL)) {
    if (typ_isa(a, TBI_BOOL_COMPATIBLE)) {
      typ_link_tentative(a, b);
      *success = true;
    }
  } else if (typ_equal(b, TBI_STRING)) {
    if (typ_isa(a, TBI_STRING_COMPATIBLE)) {
      typ_link_tentative(a, b);
      *success = true;
    }
  } else if (same_generic_functor(mod, b, TBI_SLICE)) {
    if (typ_isa(a, TBI_ARRAY_CTOR)) {
      typ_link_tentative(typ_generic_arg(a, 0), typ_generic_arg(b, 0));
      typ_link_tentative(a, b);
      *success = true;
    }
  }

  return 0;
}

static struct typ *merge_defincomplete(struct module *mod, const struct node *for_error,
                                       const struct node *a, const struct node *b) {
  struct node *dinc = defincomplete_create(mod, for_error);

  if (a->as.DEFINCOMPLETE.ident != ID__NONE) {
    defincomplete_set_ident(mod, a->as.DEFINCOMPLETE.ident_for_error,
                            dinc, a->as.DEFINCOMPLETE.ident);
  } else if (b->as.DEFINCOMPLETE.ident != ID__NONE) {
    defincomplete_set_ident(mod, b->as.DEFINCOMPLETE.ident_for_error,
                            dinc, b->as.DEFINCOMPLETE.ident);
  }

  const struct node *a_isalist = subs_at_const(a, IDX_ISALIST);
  FOREACH_SUB_CONST(isa, a_isalist) {
    defincomplete_add_isa(mod, isa, dinc, isa->typ);
  }
  const struct node *b_isalist = subs_at_const(b, IDX_ISALIST);
  FOREACH_SUB_CONST(isa, b_isalist) {
    defincomplete_add_isa(mod, isa, dinc, isa->typ);
  }

  FOREACH_SUB_CONST(f, a) {
    if (f->which == DEFFIELD) {
      defincomplete_add_field(mod, f, dinc, node_ident(f), f->typ);
    }
  }
  FOREACH_SUB_CONST(f, b) {
    if (f->which == DEFFIELD) {
      defincomplete_add_field(mod, f, dinc, node_ident(f), f->typ);
    }
  }

  error e = defincomplete_catchup(mod, dinc);
  assert(!e);
  return dinc->typ;
}

HTABLE_SPARSE(ident_typ_map, struct typ *, ident);
IMPLEMENT_HTABLE_SPARSE(unused__ static, ident_typ_map, struct typ *, ident,
                        ident_hash, ident_cmp);

static ERROR unify_two_defincomplete(struct module *mod,
                                     const struct node *for_error,
                                     struct typ *a, struct typ *b) {
  error e;
  const char *reason;

  struct node *da = typ_definition_nooverlay(a);
  struct node *db = typ_definition_nooverlay(b);
  if (da->as.DEFINCOMPLETE.ident != ID__NONE
      && db->as.DEFINCOMPLETE.ident != ID__NONE) {
    reason = "conflicting idents";
    goto except;
  }

  struct ident_typ_map map;
  ident_typ_map_init(&map, 0);
  ident_typ_map_set_delete_val(&map, false);

  FOREACH_SUB_CONST(f, da) {
    if (f->which != DEFFIELD) {
      continue;
    }
    ident_typ_map_set(&map, node_ident(subs_first_const(f)),
                      subs_at_const(f, 1)->typ);
  }

  FOREACH_SUB_CONST(f, db) {
    if (f->which != DEFFIELD) {
      continue;
    }
    struct typ **existing = ident_typ_map_get(&map, node_ident(subs_first_const(f)));
    if (existing != NULL) {
      reason = "conflicting fields";
      e = unify(mod, for_error, *existing, subs_at_const(f, 1)->typ);
      GOTO_EXCEPT(e);
    }
  }

  struct typ *r = merge_defincomplete(mod, for_error, da, db);
  typ_link_tentative(r, a);
  typ_link_tentative(r, b);

  ident_typ_map_destroy(&map);
  return 0;

except:
  assert(e == EINVAL);
  char sa[2048], sb[2048];
  snprint_defincomplete(sa, ARRAY_SIZE(sa), mod, da);
  snprint_defincomplete(sb, ARRAY_SIZE(sb), mod, db);

  e = mk_except_type(mod, for_error,
                     "%s for incomplete types\n%sand\n%s",
                     reason, sa, sb);
  THROW(e);
}

static ERROR unify_with_defunknownident(struct module *mod, const struct node *for_error,
                                        const struct typ *a, struct node *dinc) {
  assert(dinc->which == DEFINCOMPLETE);
  ident unk = dinc->as.DEFINCOMPLETE.ident;

  error e;
  if (typ_definition_which(a) != DEFTYPE
      || (typ_definition_deftype_kind(a) != DEFTYPE_ENUM
          && typ_definition_deftype_kind(a) != DEFTYPE_UNION)) {
    char *s = pptyp(mod, a);
    e = mk_except_type(mod, for_error,
                       "type '%s' is not an enum or union: cannot resolve ident '%s'",
                       s, idents_value(mod->gctx, unk));
    free(s);
    THROW(e);
  }

  struct tit *exists = typ_definition_one_member(a, unk);
  if (exists == NULL) {
    char *s = pptyp(mod, a);
    e = mk_except_type(mod, for_error, "cannot resolve ident '%s'"
                       " in enum or union type '%s'",
                       idents_value(mod->gctx, unk), s);
    free(s);
    THROW(e);
  }

  // Will typ_link_tentative() in unify_with_defincomplete().

  return 0;
}

static bool has_variant_with_field(struct module *mod,
                                   const struct node *for_error,
                                   const struct typ *t, ident name) {
  struct tit *dc = typ_definition_members(t, DEFCHOICE, 0);
  while (tit_next(dc)) {
    if (!tit_defchoice_is_leaf(dc)) {
      continue;
    }

    struct tit *f = tit_defchoice_lookup_field(dc, name);
    if (f != NULL) {
      tit_next(f);
      tit_next(dc);
      return true;
    }
  }

  tit_next(dc);
  return false;
}

// Exported for the sole benefit of finalize_defincomplete_unification().
error unify_with_defincomplete_entrails(struct module *mod,
                                        const struct node *for_error,
                                        struct typ *a,
                                        struct typ *inc) {
  error e;

  struct node *dinc = typ_definition_nooverlay(inc);
  if (dinc->as.DEFINCOMPLETE.ident != ID__NONE) {
    e = unify_with_defunknownident(mod, for_error, a, dinc);
    EXCEPT(e);
  }

  const bool is_union = typ_definition_which(a) == DEFTYPE
    && typ_definition_deftype_kind(a) == DEFTYPE_UNION;
  if (is_union) {
    FOREACH_SUB_CONST(f, dinc) {
      if (f->which != DEFFIELD) {
        continue;
      }

      if (!has_variant_with_field(mod, for_error, a, node_ident(f))) {
        e = mk_except_type(mod, for_error, "cannot resolve field '%s' "
                           "in any variant of the union",
                           idents_value(mod->gctx, node_ident(f)));
        (void) e;
        e = mk_except_type_unification(mod, for_error, a, inc);
        THROW(e);
      }
    }

    return 0;
  }

  // FIXME: if 'a' is a tentative intf, we should be adding that intf as a
  // restriction on 'dinc'.

  FOREACH_SUB_CONST(f, dinc) {
    if (f->which != DEFFIELD) {
      continue;
    }

    struct tit *af = typ_definition_one_member(a, node_ident(f));
    e = unify_refcompat(mod, for_error, tit_typ(af), f->typ);
    EXCEPT(e);
    tit_next(af);
  }

  return 0;
}

static ERROR unify_with_defincomplete(struct module *mod,
                                      const struct node *for_error,
                                      struct typ *a, struct typ *inc) {
  error e = unify_with_defincomplete_entrails(mod, for_error, a, inc);
  EXCEPT(e);

  typ_link_tentative(a, inc);

  return 0;
}

static ERROR unify_defincomplete(struct module *mod,
                                 const struct node *for_error,
                                 struct typ *a, struct typ *b,
                                 bool a_inc, bool b_inc) {
  error e;
  if (a_inc && b_inc) {
    e = unify_two_defincomplete(mod, for_error, a, b);
    EXCEPT(e);

    return 0;
  }

  if (a_inc && !b_inc) {
    SWAP(a, b);
    SWAP(a_inc, b_inc);
  }

  e = unify_with_defincomplete(mod, for_error, a, b);
  EXCEPT(e);

  return 0;
}
static ERROR unify_with_equal(struct module *mod, const struct node *for_error,
                              struct typ *a, struct typ *b) {
  if (a == b) {
    return 0;
  }

  error e = typ_check_equal(mod, for_error, a, b);
  EXCEPT(e);

  if (!typ_is_tentative(b)) {
    SWAP(b, a);
  }

  assert(typ_generic_arity(a) == typ_generic_arity(b));
  const size_t arity = typ_generic_arity(a);
  for (size_t n = 0; n < arity; ++n) {
    struct typ *arga = typ_generic_arg(a, n);
    struct typ *argb = typ_generic_arg(b, n);

    error e = unify_with_equal(mod, for_error, arga, argb);
    assert(!e);
  }

  if (typ_is_tentative(b)) {
    if (typ_is_generic_functor(b)) {
      typ_link_tentative_functor(mod, a, b);
    } else {
      typ_link_tentative(a, b);
    }
  } else {
//    assert(a == b);
  }

  return 0;
}

static ERROR check_reference_compatibility(struct module *mod,
                                           const struct node *for_error,
                                           const struct typ *a,
                                           const struct typ *target) {
  if (typ_equal(target, TBI_ANY_ANY_REF)) {
    return 0;
  }

  const struct typ *a0 = typ_generic_functor_const(a);
  const struct typ *target0 = typ_generic_functor_const(target);

  bool ok = false;
  error e;
  if (typ_equal(target0, TBI_ANY_REF)) {
    ok = typ_isa(a0, TBI_ANY_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_REF);;
  } else if (typ_equal(target0, TBI_ANY_MUTABLE_REF)) {
    ok = typ_isa(a0, TBI_ANY_MUTABLE_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_MUTABLE_REF);;
  } else if (typ_equal(target0, TBI_ANY_NULLABLE_REF)) {
    ok = typ_isa(a0, TBI_ANY_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_REF);
  } else if (typ_equal(target0, TBI_ANY_NULLABLE_MUTABLE_REF)) {
    ok = typ_isa(a0, TBI_ANY_MUTABLE_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_MUTABLE_REF);

  } else if (typ_equal(target0, TBI_REF)) {
    ok = typ_isa(a0, TBI_ANY_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_REF);;
  } else if (typ_equal(target0, TBI_MREF)) {
    ok = typ_isa(a0, TBI_ANY_MUTABLE_REF)
      || typ_isa(a0, TBI_ANY_NULLABLE_MUTABLE_REF);;
  } else if (typ_equal(target0, TBI_MMREF)) {
    ok = typ_equal(a0, TBI_MMREF)
      || typ_equal(a0, TBI_NMMREF);
  } else if (typ_equal(target0, TBI_NREF)) {
    ok = typ_isa(a0, TBI_ANY_NULLABLE_REF)
      || typ_isa(a0, TBI_ANY_REF);
  } else if (typ_equal(target0, TBI_NMREF)) {
    ok = typ_isa(a0, TBI_ANY_NULLABLE_MUTABLE_REF)
      || typ_isa(a0, TBI_ANY_MUTABLE_REF);
  } else if (typ_equal(target0, TBI_NMMREF)) {
    ok = typ_equal(a0, TBI_NMMREF)
      || typ_equal(a0, TBI_MMREF);
  }

  if (!ok) {
    e = mk_except_type_unification(mod, for_error, a, target);
    THROW(e);
  }

  return 0;
}

static ERROR unify_dyn(struct module *mod, const struct node *for_error,
                       struct typ *a, struct typ *b,
                       bool a_intf, bool b_intf) {
  if (!b_intf) {
    SWAP(a, b);
    SWAP(a_intf, b_intf);
  }

  error e = typ_check_isa(mod, for_error, a, b);
  EXCEPT(e);

  return 0;
}

static ERROR unify_reforslice_arg(struct module *mod, uint32_t flags,
                                  const struct node *for_error,
                                  struct typ *a, struct typ *b) {
  struct typ *arg_a = typ_generic_arg(a, 0);
  struct typ *arg_b = typ_generic_arg(b, 0);
  const bool arg_a_intf = typ_definition_which(arg_a) == DEFINTF;
  const bool arg_b_intf = typ_definition_which(arg_b) == DEFINTF;
  const bool arg_a_tentative = typ_is_tentative(arg_a);
  const bool arg_b_tentative = typ_is_tentative(arg_b);
  const bool arg_a_weak = typ_is_weakly_concrete(arg_a);
  const bool arg_b_weak = typ_is_weakly_concrete(arg_b);

  error e;
  if ((arg_a_intf || arg_b_intf)
      && ((!arg_a_tentative && !arg_b_tentative)
          || (!arg_a_tentative && arg_b_weak)
          || (!arg_b_tentative && arg_a_weak))) {
    e = unify_dyn(mod, for_error, arg_a, arg_b, arg_a_intf, arg_b_intf);
    EXCEPT(e);
  } else {
    e = do_unify(mod, flags, for_error, arg_a, arg_b);
    EXCEPT(e);
  }

  return 0;
}

void unify_with_new_parent(struct module *mod, const struct node *for_error,
                           struct typ *p, struct typ *t) {
  assert(typ_is_tentative(t));

  struct typ *tm = typ_member(p, typ_definition_ident(t));
  if (tm == NULL) {
    // FIXME: this may be the member of a weakly concrete that has no match
    // in the concrete type it's being linked to. We should have prevented
    // that with proper intf requirements.
    return;
  }

  const size_t arity = typ_generic_arity(t);
  if (arity == 0) {
    typ_link_tentative(tm, t);
    return;
  }

  struct typ **args = calloc(arity, sizeof(struct typ *));
  for (size_t n = 0; n < arity; ++n) {
    args[n] = typ_generic_arg(t, n);
  }

  struct node *i = NULL;
  error e = instantiate(&i, mod, for_error, -1, tm, args, arity, false);
  assert(!e);
  typ_link_tentative(i->typ, t);
}

struct typ *unify_with_new_functor(struct module *mod, const struct node *for_error,
                                   struct typ *f, struct typ *t) {
  assert(typ_is_generic_functor(f));
  assert(!typ_is_generic_functor(t));
  assert(typ_generic_arity(t) == typ_generic_arity(f));

  struct typ *t0 = typ_generic_functor(t);
  assert(typ_is_tentative(t));
  assert(typ_is_tentative(t0));

  struct node *i = instantiate_fully_implicit(mod, for_error, f);

  for (size_t n = 0, arity = typ_generic_arity(t); n < arity; ++n) {
    typ_link_tentative(typ_generic_arg(t, n), typ_generic_arg(i->typ, n));
  }

  typ_link_tentative(i->typ, t);

  return i->typ;
}

static ERROR unify_reference_with_refcompat(struct module *mod, uint32_t flags,
                                            const struct node *for_error,
                                            struct typ *a, struct typ *b,
                                            bool a_refcompat,
                                            bool b_refcompat) {
  assert(a_refcompat ^ b_refcompat);

  if (a_refcompat) {
    SWAP(a, b);
    SWAP(a_refcompat, b_refcompat);
    SWAP_FLAGS(flags);
  }

  error e = check_reference_compatibility(mod, for_error, a, b);
  EXCEPT(e);

  if (typ_equal(b, TBI_ANY_ANY_REF)) {
    typ_link_tentative(a, b);
    return 0;
  }

  if (typ_equal(a, b)) {
    e = unify_with_equal(mod, for_error, a, b);
    EXCEPT(e);
    return 0;
  }

  e = unify_reforslice_arg(mod, flags, for_error, a, b);
  EXCEPT(e);

  struct typ *a0 = typ_generic_functor(a);
  struct typ *b0 = typ_generic_functor(b);
  if (typ_definition_which(b0) == DEFINTF && typ_is_tentative(b0)
      && typ_isa(a0, b0)) {
    typ_link_tentative_functor(mod, a0, b0);
  } else if (typ_definition_which(a0) == DEFINTF && typ_is_tentative(a0)
             && typ_isa(b0, a0)) {
    typ_link_tentative_functor(mod, b0, a0);
  }

  return 0;
}

static ERROR unify_reference(struct module *mod, uint32_t flags,
                             const struct node *for_error,
                             struct typ *a, struct typ *b,
                             bool a_ref, bool b_ref) {
  error e;

  if (!a_ref) {
    SWAP(a, b);
    SWAP(a_ref, b_ref);
    SWAP_FLAGS(flags);
  }

  if (!b_ref && typ_is_tentative(b) && typ_isa(a, b)) {
    typ_link_tentative(a, b);
    return 0;
  }

  if (!b_ref) {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  const bool a_refcompat = flags & REFCOMPAT_LEFT;
  const bool b_refcompat = flags & REFCOMPAT_RIGHT;
  if (a_refcompat || b_refcompat) {
    e = unify_reference_with_refcompat(mod, flags, for_error, a, b,
                                       a_refcompat, b_refcompat);
    EXCEPT(e);
    return 0;
  }

  if (typ_equal(a, TBI_ANY_ANY_REF)) {
    typ_link_tentative(b, a);
    return 0;
  }

  if (typ_equal(b, TBI_ANY_ANY_REF)) {
    typ_link_tentative(a, b);
    return 0;
  }

  struct typ *a0 = typ_generic_functor(a);
  struct typ *b0 = typ_generic_functor(b);

  if (typ_isa(b0, a0)) {
    SWAP(a, b);
    SWAP(a0, b0);
    SWAP(a_ref, b_ref);
    SWAP_FLAGS(flags);
  }

  if (!typ_isa(a0, b0)) {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  e = unify_reforslice_arg(mod, flags, for_error, a, b);
  EXCEPT(e);

  if (typ_definition_which(b0) == DEFINTF && typ_is_tentative(b0)) {
    typ_link_tentative_functor(mod, a0, b0);
  }

  return 0;
}

//static error typ_ref(struct typ **result,
//                     struct module *mod, struct node *for_error,
//                     enum token_type op, struct typ *typ);
//
//EXAMPLE_NCC(unify_with_reference_literal) {
//  struct node *foo = mock_deffun(mod, "foo");
//  struct node *body = foo->subs[foo->subs_count-1];
//  mock_parse(mod, body, "return (@1).");
//
//  G(intlit, body, NUMBER);
//  intlit->as.NUMBER.value = "1";
//  intlit->typ = typ_create_tentative(TBI_LITERALS_INTEGER);
//  struct node *for_error = intlit;
//
//  struct typ *rintlit = typ_create(NULL);
//  error e = typ_ref(&rintlit, mod, for_error, TREFDOT, intlit->typ);
//  assert(!e);
//  assert(typ_is_tentative(rintlit));
//
//  struct typ *drintlit = typ_generic_arg(rintlit, 0);
//  assert(typ_equal(drintlit, TBI_LITERALS_INTEGER));
//  assert(typ_is_tentative(drintlit));
//
//  struct typ *i32 = TBI_I32;
//  struct typ *ri32 = typ_create(NULL);
//  e = typ_ref(&ri32, mod, for_error, TREFDOT, i32);
//  assert(!e);
//  assert(typ_is_tentative(ri32));
//
//  assert(typ_equal(ri32, rintlit));
//}

static ERROR check_slice_compatibility(struct module *mod,
                                       const struct node *for_error,
                                       const struct typ *a,
                                       const struct typ *target) {
  if (typ_equal(target, TBI_ANY_ANY_SLICE)) {
    return 0;
  }

  const struct typ *a0 = typ_generic_functor_const(a);
  const struct typ *target0 = typ_generic_functor_const(target);

  bool ok = false;
  error e;
  if (typ_equal(target0, TBI_ANY_SLICE)) {
    ok = typ_isa(a0, TBI_ANY_SLICE);
  } else if (typ_equal(target0, TBI_ANY_MUTABLE_SLICE)) {
    ok = typ_isa(a0, TBI_ANY_MUTABLE_SLICE);

  } else if (typ_equal(target0, TBI_SLICE)) {
    ok = typ_isa(a0, TBI_ANY_SLICE);
  } else if (typ_equal(target0, TBI_MSLICE)) {
    ok = typ_isa(a0, TBI_ANY_MUTABLE_SLICE);
  }

  if (!ok) {
    e = mk_except_type_unification(mod, for_error, a, target);
    THROW(e);
  }

  return 0;
}

static ERROR unify_slice_with_refcompat(struct module *mod, uint32_t flags,
                                        const struct node *for_error,
                                        struct typ *a, struct typ *b,
                                        bool a_refcompat,
                                        bool b_refcompat) {
  if (a_refcompat) {
    SWAP(a, b);
    SWAP(a_refcompat, b_refcompat);
    SWAP_FLAGS(flags);
  }

  error e = check_slice_compatibility(mod, for_error, a, b);
  EXCEPT(e);

  if (typ_equal(b, TBI_ANY_ANY_SLICE)) {
    typ_link_tentative(a, b);
    return 0;
  }

  if (typ_equal(a, b)) {
    e = unify_with_equal(mod, for_error, a, b);
    EXCEPT(e);
    return 0;
  }

  e = unify_reforslice_arg(mod, flags, for_error, a, b);
  EXCEPT(e);

  struct typ *b0 = typ_generic_functor(b);
  if (typ_definition_which(b0) == DEFINTF && typ_is_tentative(b0)) {
    struct typ *a0 = typ_generic_functor(a);
    typ_link_tentative_functor(mod, a0, b0);
  }

  return 0;
}

static ERROR unify_slice(struct module *mod, uint32_t flags,
                         const struct node *for_error,
                         struct typ *a, struct typ *b,
                         bool a_slice, bool b_slice) {
  error e;

  if (!a_slice) {
    SWAP(a, b);
    SWAP(a_slice, b_slice);
    SWAP_FLAGS(flags);
  }

  if (!b_slice && typ_is_tentative(b) && typ_isa(a, b)) {
    typ_link_tentative(a, b);
    return 0;
  }

  if (!b_slice) {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  const bool a_refcompat = flags & REFCOMPAT_LEFT;
  const bool b_refcompat = flags & REFCOMPAT_RIGHT;
  if (a_refcompat || b_refcompat) {
    e = unify_slice_with_refcompat(mod, flags, for_error, a, b,
                                   a_refcompat, b_refcompat);
    EXCEPT(e);
    return 0;
  }

  if (typ_equal(a, TBI_ANY_ANY_SLICE)) {
    typ_link_tentative(b, a);
    return 0;
  }

  if (typ_equal(b, TBI_ANY_ANY_SLICE)) {
    typ_link_tentative(a, b);
    return 0;
  }

  struct typ *a0 = typ_generic_functor(a);
  struct typ *b0 = typ_generic_functor(b);

  if (typ_isa(b0, a0)) {
    SWAP(a, b);
    SWAP(a0, b0);
    SWAP(a_slice, b_slice);
    SWAP_FLAGS(flags);
  }

  if (!typ_isa(a0, b0)) {
    e = mk_except_type_unification(mod, for_error, a, b);
    THROW(e);
  }

  e = unify_reforslice_arg(mod, flags, for_error, a, b);
  EXCEPT(e);

  if (typ_definition_which(b0) == DEFINTF && typ_is_tentative(b0)) {
    typ_link_tentative_functor(mod, a0, b0);
  }

  return 0;
}

static void unify_with_any(struct module *mod, const struct node *for_error,
                           struct typ *a, struct typ *b,
                           bool a_is_any, bool b_is_any) {
  if (a_is_any) {
    SWAP(a, b);
    SWAP(a_is_any, b_is_any);
  }

  typ_link_tentative(a, b);
}

static ERROR do_unify(struct module *mod, uint32_t flags,
                      const struct node *for_error,
                      struct typ *a, struct typ *b) {
  BEGTIMEIT(TIMEIT_UNIFY);
  error e;

  bool a_tentative = typ_is_tentative(a);
  bool b_tentative = typ_is_tentative(b);

  if (!(flags & (REFCOMPAT_LEFT | REFCOMPAT_RIGHT))
      && !a_tentative && !b_tentative) {
    e = typ_check_equal(mod, for_error, a, b);
    EXCEPT(e);
    goto ok;
  }

  if (a_tentative && !b_tentative) {
    SWAP(a, b);
    SWAP(a_tentative, b_tentative);
    SWAP_FLAGS(flags);
  }

  if (typ_equal(a, b)) {
    e = unify_with_equal(mod, for_error, a, b);
    EXCEPT(e);
    goto ok;
  }

  const bool a_is_any = typ_equal(a, TBI_ANY);
  const bool b_is_any = typ_equal(b, TBI_ANY);
  if (a_is_any || b_is_any) {
    unify_with_any(mod, for_error, a, b, a_is_any, b_is_any);
    goto ok;
  }

  const bool a_inc = typ_definition_which(a) == DEFINCOMPLETE;
  const bool b_inc = typ_definition_which(b) == DEFINCOMPLETE;
  if (a_inc || b_inc) {
    e = unify_defincomplete(mod, for_error, a, b, a_inc, b_inc);
    EXCEPT(e);
    goto ok;
  }

  const bool a_literal = typ_is_literal(a);
  const bool b_literal = typ_is_literal(b);
  if (a_literal || b_literal) {
    e = unify_literal(mod, flags, for_error, a, b, a_literal, b_literal);
    EXCEPT(e);
    goto ok;
  }

  const bool a_ref = typ_is_reference(a);
  const bool b_ref = typ_is_reference(b);
  if (a_ref || b_ref) {
    e = unify_reference(mod, flags, for_error, a, b, a_ref, b_ref);
    EXCEPT(e);
    goto ok;
  }

  const bool a_weakly_concrete = typ_is_weakly_concrete(a);
  const bool b_weakly_concrete = typ_is_weakly_concrete(b);
  if (a_weakly_concrete || b_weakly_concrete) {
    bool success = false;
    e = unify_with_weakly_concrete(&success,
                                   mod, for_error, a, b,
                                   a_weakly_concrete, b_weakly_concrete);
    EXCEPT(e);

    if (success) {
      goto ok;
    }
  }

  const bool a_slice = typ_is_slice(a);
  const bool b_slice = typ_is_slice(b);
  if (a_slice && b_slice) {
    e = unify_slice(mod, flags, for_error, a, b, a_slice, b_slice);
    EXCEPT(e);
    goto ok;
  }

  const bool a_non_generic = typ_generic_arity(a) == 0;
  const bool b_non_generic = typ_generic_arity(b) == 0;
  if (a_non_generic || b_non_generic) {
    e = unify_non_generic(mod, for_error, a, b, a_non_generic, b_non_generic);
    EXCEPT(e);
    goto ok;
  }

  e = unify_generics(mod, flags, for_error, a, b, a_tentative, b_tentative);
  EXCEPT(e);

ok:
  ENDTIMEIT(true, TIMEIT_UNIFY);
  return 0;
}

error unify(struct module *mod, const struct node *for_error,
            struct typ *a, struct typ *b) {
  error e = do_unify(mod, 0, for_error, a, b);
  EXCEPT(e);
  e = process_finalizations();
  EXCEPT(e);
  return 0;
}

static bool needs_refcompat(const struct typ *t) {
  if (typ_isa(t, TBI_ANY_TUPLE)) {
    for (size_t n = 0, arity = typ_generic_arity(t); n < arity; ++n) {
      if (needs_refcompat(typ_generic_arg_const(t, n))) {
        return true;
      }
    }
    return false;
  }

  return typ_is_reference(t) || typ_is_slice(t);
}

// Be tolerant of acceptable differences in reference functors.
error unify_refcompat(struct module *mod, const struct node *for_error,
                      struct typ *target, struct typ *b) {
  error e;

  if (needs_refcompat(target) || needs_refcompat(b)) {
    e = do_unify(mod, REFCOMPAT_LEFT, for_error, target, b);
    EXCEPT(e);
    e = process_finalizations();
    EXCEPT(e);
    return 0;
  }

  e = unify(mod, for_error, target, b);
  EXCEPT(e);
  return 0;
}

EXAMPLE_NCC(unify) {
//  struct node *for_error = calloc(1, sizeof(struct node));
//  error e;
//  {
//    struct typ *a = TBI_I32;
//    struct typ *b = NULL;
//    set_typ(&b, typ_create_tentative(TBI_INTEGER));
//    e = unify(mod, for_error, a, b);
//    assert(!e);
//  }
//  {
//    struct typ *a = TBI_I32;
//    struct typ *b = TBI_U32;
//    e = unify(mod, for_error, a, b);
//    assert(e);
//  }
}
