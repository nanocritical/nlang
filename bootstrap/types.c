#include "types.h"

#include "table.h"
#include "mock.h"
#include "typset.h"
#include "unify.h"
#include "inference.h"
#include "instantiate.h"
#include "parser.h"

#include <stdio.h>
#include <stdarg.h>

static size_t def_generic_arity(const struct typ *t);
static struct typ *def_generic_arg(struct typ *t, size_t n);

#define BACKLINKS_LEN 7 // arbitrary
#define USERS_LEN 7 // arbitrary

struct backlinks {
  size_t count;
  struct typ **links[BACKLINKS_LEN];
  struct backlinks *more;
};

struct users {
  size_t count;
  struct typ *users[USERS_LEN];
  struct users *more;
};

enum typ_flags {
  TYPF_TENTATIVE = 0x1,
  TYPF_BUILTIN = 0x2,
  TYPF_PSEUDO_BUILTIN = 0x4,
  TYPF_TRIVIAL = 0x8,
  TYPF_REF = 0x10,
  TYPF_NREF = 0x20,
  TYPF_SLICE = 0x40,
  TYPF_LITERAL = 0x80,
  TYPF_CONCRETE = 0x200,
  TYPF_GENARG = 0x400,
  TYPF__INHERIT_FROM_FUNCTOR = TYPF_TENTATIVE
    | TYPF_BUILTIN | TYPF_PSEUDO_BUILTIN
    | TYPF_TRIVIAL | TYPF_REF | TYPF_NREF
    | TYPF_LITERAL | TYPF_CONCRETE,
  TYPF__MASK_HASH = 0xffff & ~(TYPF_TENTATIVE | TYPF_CONCRETE | TYPF_GENARG),
};

enum rdy_flags {
  RDY_HASH = 0x1,
  RDY_GEN = 0x2,
};

static uint32_t typ_ptr_hash(const struct typ **a) {
  return hash32_hsieh(a, sizeof(*a));
}

static int typ_ptr_cmp(const struct typ **a, const struct typ **b) {
  return memcmp(a, b, sizeof(*a));
}

HTABLE_SPARSE(typ2loc, struct typ **, struct typ *);
IMPLEMENT_HTABLE_SPARSE(unused__ static, typ2loc, struct typ **, struct typ *,
                        typ_ptr_hash, typ_ptr_cmp);

struct overlay {
  struct typ2loc map;
};

struct typ {
  uint32_t flags;
  uint32_t hash;

  struct node *definition;
  struct overlay *overlay;

  uint8_t rdy;

  struct typ *gen0;
  size_t gen_arity;
  struct typ **gen_args; // of length gen_arity (generics)

  struct typset quickisa;

  struct backlinks backlinks;
  struct users users;
};

static struct node *typ_definition(struct typ *t) {
  return t->definition;
}

static const struct node *typ_definition_const(const struct typ *t) {
  return typ_definition(CONST_CAST(t));
}

#define FOREACH_BACKLINK(idx, back, t, what) do { \
  struct backlinks *backlinks = &t->backlinks; \
  do { \
    for (size_t idx = 0; idx < backlinks->count; ++idx) { \
      struct typ **back = backlinks->links[idx]; \
      if (back != NULL) { \
        what; \
      } \
    } \
    backlinks = backlinks->more; \
  } while (backlinks != NULL); \
} while (0)

#define FOREACH_USER(idx, user, t, what) do { \
  struct users *users = &t->users; \
  do { \
    for (size_t idx = 0; idx < users->count; ++idx) { \
      struct typ *user = users->users[idx]; \
      if (user != NULL) { \
        what; \
      } \
    } \
    users = users->more; \
  } while (users != NULL); \
} while (0)

static bool typ_is_genarg(const struct typ *t) {
  return t->flags & TYPF_GENARG;
}

static void overlay_init(struct typ *t) {
  t->overlay = calloc(1, sizeof(*t->overlay));
  typ2loc_init(&t->overlay->map, 0);
}

static void overlay_map(struct typ *t, struct typ **dst, struct typ *src) {
  assert(typ_is_genarg(src));
  const bool no = typ2loc_set(&t->overlay->map, src, dst);
  assert(!no);
}

static struct typ *overlay_translate(struct typ *t, struct typ *src) {
  if (!typ_is_genarg(src)) {
    return src;
  }

  struct typ ***existing = typ2loc_get(&t->overlay->map, src);
  if (existing != NULL) {
    return **existing;
  } else {
    return src;
  }
}

#define OLAY(t, src) ( ((t)->overlay != NULL) ? (overlay_translate(t, src)) : (src) )

bool typ_hash_ready(const struct typ *t) {
  return t->rdy & RDY_HASH;
}

static uint32_t typ_hash(const struct typ **a) {
  // The 'hash' in a tentative typ is maintained as the typ gets linked to a
  // new typs. So 'hash' can be used in typ_equal(), etc. But it cannot be
  // used in a hash table *IF* elements of the table are re-linked while the
  // table exists. fintypset can be used in typ_isalist_foreach() with
  // tentative types when typs are not actively being linked. E.g.
  // unify_generics() typ_isalist_each(find_instance_of) is fine. So we
  // cannot assert(!typ_is_tentative(*a)) in general.
  //
  // FIXME: this is a difficult condition to respect.
  assert(typ_hash_ready(*a));
  const uint32_t h = (*a)->hash;
  return h;
}

static int typ_cmp(const struct typ **a, const struct typ **b) {
  return !typ_equal(*a, *b);
}

IMPLEMENT_HTABLE_SPARSE(, fintypset, uint32_t, struct typ *,
                        typ_hash, typ_cmp);

static void remove_backlink(struct typ *t, struct typ **loc) {
  FOREACH_BACKLINK(idx, back, t,
                   if (back == loc) { backlinks->links[idx] = NULL; });
}

size_t typ_debug_backlinks_count(const struct typ *t) {
  const struct backlinks *backlinks = &t->backlinks;
  size_t c = 0;
  while (backlinks->more != NULL) {
    backlinks = backlinks->more;
    c += 7;
  }

  return c + backlinks->count;
}

void unset_typ(struct typ **loc) {
  assert(*loc != NULL);

  remove_backlink(*loc, loc);
  *loc = NULL;
}

static void add_backlink(struct typ *t, struct typ **loc) {
  if (!typ_is_tentative(t)) {
    return;
  }

  struct backlinks *backlinks = &t->backlinks;
  while (backlinks->more != NULL) {
    backlinks = backlinks->more;
  }

  if (backlinks->count == BACKLINKS_LEN) {
    backlinks->more = calloc(1, sizeof(*backlinks->more));
    backlinks = backlinks->more;
  }

  backlinks->links[backlinks->count] = loc;
  backlinks->count += 1;
}

void set_typ(struct typ **loc, struct typ *t) {
  assert(t != NULL);
  assert(*loc == NULL
         || *loc == t
         || *loc == TBI__NOT_TYPEABLE
         || *loc == TBI__CALL_FUNCTION_SLOT
         || *loc == TBI__MERCURIAL || *loc == TBI__MUTABLE);

  *loc = t;

  add_backlink(t, loc);
}

void typ_add_tentative_bit__privileged(struct typ **loc) {
  if (!typ_is_tentative(*loc)) {
    (*loc)->flags |= TYPF_TENTATIVE;
    add_backlink(*loc, loc);
  }
}

void typ_declare_final__privileged(struct typ *t) {
  for (size_t n = 0, arity = typ_generic_arity(t); n < arity; ++n) {
    struct typ *arg = typ_generic_arg(t, n);
    typ_declare_final__privileged(arg);
  }
  t->flags &= ~TYPF_TENTATIVE;
}

static void clear_backlinks(struct typ *t) {
  struct backlinks *b = t->backlinks.more;

  while (b != NULL) {
    struct backlinks *m = b->more;
    free(b);
    b = m;
  }

  memset(&t->backlinks, 0, sizeof(t->backlinks));
}

static void add_user(struct typ *arg, struct typ *user) {
  if (arg == user) {
    return;
  }

  if (!typ_is_tentative(arg)) {
    return;
  }

  assert(typ_is_tentative(user));

  struct users *users = &arg->users;
  while (users->more != NULL) {
    users = users->more;
  }

  if (users->count == USERS_LEN) {
    users->more = calloc(1, sizeof(*users->more));
    users = users->more;
  }

  // Do not use set_typ(): that would create ordering problems in
  // link_to_final()/link_to_tentative() between the processing of regular
  // backlinks, and users.
  users->users[users->count] = user;
  users->count += 1;
}

static void clear_users(struct typ *t) {
  struct users *b = t->users.more;

  while (b != NULL) {
    struct users *m = b->more;
    free(b);
    b = m;
  }

  memset(&t->users, 0, sizeof(t->users));
}

void fintypset_fullinit(struct fintypset *set) {
  fintypset_init(set, 0);
  fintypset_set_delete_val(set, -1);
}

static void quickisa_init(struct typ *t) {
  typset_init(&t->quickisa);
}

// Examples of non-concrete types: uninstantiated generics or instances
// within an instantiated generic which are instantiated over non-tentative
// interfaces.
static void create_update_concrete_flag(struct typ *t) {
  if (typ_is_generic_functor(t)) {
    t->flags &= ~TYPF_CONCRETE;
  } else if (typ_generic_arity(t) == 0) {
    if (typ_definition_const(t)->which == DEFINTF) {
      t->flags &= ~TYPF_CONCRETE;
    } else {
      t->flags |= TYPF_CONCRETE;
    }
  } else if (typ_is_reference(t)) {
    if (typ_definition_const(t)->which == DEFINTF) {
      t->flags &= ~TYPF_CONCRETE;
    } else {
      const struct typ *arg = typ_generic_arg_const(t, 0);
      if (typ_generic_arity(arg) == 0) {
        // Even if the generic argument is a DEFINTF, then t is a dyn, which
        // is concrete.
        t->flags |= TYPF_CONCRETE;
      } else {
        if (typ_is_concrete(arg)) {
          t->flags |= TYPF_CONCRETE;
        } else {
          t->flags &= ~TYPF_CONCRETE;
        }
      }
    }
  } else {
    t->flags |= TYPF_CONCRETE;
    for (size_t n = 0, arity = typ_generic_arity(t); n < arity; ++n) {
      const struct typ *arg = typ_generic_arg_const(t, n);
      if (arg == NULL) {
        // Too early to tell
        break;
      }
      if (typ_is_generic_functor(arg)
          && typ_definition_const(arg)->which != DEFINTF) {
        continue;
      }
      if (!typ_is_concrete(arg)) {
        t->flags &= ~TYPF_CONCRETE;
        break;
      }
    }
  }
}

static void create_flags(struct typ *t, struct typ *tbi) {
  const struct node *d = typ_definition_const(t);
  if (d == NULL) {
    // This is very early in the global init.
    return;
  }

  const struct toplevel *toplevel = node_toplevel_const(d);
  const struct typ *functor = NULL;
  if (toplevel != NULL && toplevel->generic != NULL) {
    functor = toplevel->generic->our_generic_functor_typ;
  }

  if (t == TBI_LITERALS_NULL
      || t == TBI_ANY_ANY_REF) {
    t->flags |= TYPF_REF;
  }

  if (t == TBI_LITERALS_NULL) {
    t->flags |= TYPF_NREF;
  }

  const struct typ *maybe_ref = tbi;
  if (functor != NULL) {
    maybe_ref = functor;

    t->flags |= functor->flags & TYPF__INHERIT_FROM_FUNCTOR;
  }

  if (maybe_ref == TBI_ANY_REF
      || maybe_ref == TBI_ANY_MUTABLE_REF
      || maybe_ref == TBI_ANY_NULLABLE_REF
      || maybe_ref == TBI_ANY_NULLABLE_MUTABLE_REF
      || maybe_ref == TBI_REF
      || maybe_ref == TBI_MREF
      || maybe_ref == TBI_MMREF
      || maybe_ref == TBI_NREF
      || maybe_ref == TBI_NMREF
      || maybe_ref == TBI_NMMREF) {
    t->flags |= TYPF_REF;
  }

  if (maybe_ref == TBI_ANY_NULLABLE_REF
      || maybe_ref == TBI_ANY_NULLABLE_MUTABLE_REF
      || maybe_ref == TBI_NREF
      || maybe_ref == TBI_NMREF
      || maybe_ref == TBI_NMMREF) {
    t->flags |= TYPF_NREF;
  }

  if (maybe_ref == TBI_ANY_ANY_SLICE
      || maybe_ref == TBI_ANY_SLICE
      || maybe_ref == TBI_ANY_MUTABLE_SLICE
      || maybe_ref == TBI_SLICE
      || maybe_ref == TBI_MSLICE) {
    t->flags |= TYPF_SLICE;
  }

  if (d->which == IMPORT) {
    t->flags |= TYPF_PSEUDO_BUILTIN;
  }

  if (tbi == NULL) {
    return;
  }

  t->flags |= TYPF_BUILTIN;

  if (tbi == TBI_LITERALS_NULL
      || tbi == TBI_LITERALS_INTEGER
      || tbi == TBI_LITERALS_FLOATING
      || tbi == TBI__NOT_TYPEABLE
      || tbi == TBI__CALL_FUNCTION_SLOT
      || tbi == TBI__MUTABLE
      || tbi == TBI__MERCURIAL) {
    t->flags |= TYPF_PSEUDO_BUILTIN;
  }

  if (functor != NULL && typ_equal(functor, TBI_TRIVIAL_ARRAY_CTOR)) {
    t->flags |= TYPF_TRIVIAL;
  }
  if (t->flags & TYPF_REF) {
    t->flags |= TYPF_TRIVIAL;
  }
  if (tbi == TBI_TRIVIAL_CTOR
      || tbi == TBI_TRIVIAL_COPY
      || tbi == TBI_TRIVIAL_COPY_BUT_OWNED
      || tbi == TBI_TRIVIAL_COMPARE
      || tbi == TBI_TRIVIAL_EQUALITY
      || tbi == TBI_TRIVIAL_ORDER
      || tbi == TBI_TRIVIAL_DTOR
      || tbi == TBI_TRIVIAL_ARRAY_CTOR) {
    t->flags |= TYPF_TRIVIAL;
  }

  if (tbi == TBI_LITERALS_NULL
      || tbi == TBI_LITERALS_INTEGER
      || tbi == TBI_LITERALS_FLOATING) {
    t->flags |= TYPF_LITERAL;
  }
}

bool typ_was_zeroed(const struct typ *t) {
  return t->definition == NULL;
}

struct typ *typ_create(struct typ *tbi, struct node *definition) {
  struct typ *r = tbi != NULL ? tbi : calloc(1, sizeof(struct typ));

  r->definition = definition;
  set_typ(&r->gen0, r);
  create_flags(r, tbi);

  return r;
}

void typ_create_update_genargs(struct typ *t) {
  assert(!(t->rdy & RDY_GEN));

  create_update_concrete_flag(t);

  struct node *dpar = parent(typ_definition(t));
  if (NM(dpar->which) & (NM(DEFTYPE) | NM(DEFINTF))) {
    if (typ_is_tentative(dpar->typ)) {
      typ_add_tentative_bit__privileged(&typ_definition(t)->typ);
      add_user(dpar->typ, t);
    }
  }

  const size_t arity = typ_generic_arity(t);
  t->gen_arity = arity;

  if (arity == 0) {
    t->rdy |= RDY_GEN;
    return;
  }

  t->gen_args = calloc(arity, sizeof(*t->gen_args));

  unset_typ(&t->gen0);
  struct typ *t0 = typ_generic_functor(t);
  set_typ(&t->gen0, t0);
  if (typ_is_tentative(t0)) {
    typ_add_tentative_bit__privileged(&typ_definition(t)->typ);
    add_user(t0, t);
  }
  for (size_t n = 0, count = typ_generic_arity(t); n < count; ++n) {
    struct typ *arg = typ_generic_arg(t, n);
    set_typ(&t->gen_args[n], arg);
    if (typ_is_tentative(arg)) {
      typ_add_tentative_bit__privileged(&typ_definition(t)->typ);
      add_user(arg, t);
    }
  }

  t->rdy |= RDY_GEN;
}

void typ_create_update_hash(struct typ *t) {
  assert(!typ_hash_ready(t) || typ_is_tentative(t));
  assert(t->rdy & RDY_GEN);

#define LEN 8 // arbitrary, at least 4
  uint32_t buf[LEN] = { 0 };
  buf[0] = t->flags & TYPF__MASK_HASH;
  buf[1] = node_ident(typ_definition_const(t));
  buf[2] = t->gen_arity;
  size_t i = 3;
  for (size_t n = 0; n < t->gen_arity; ++n) {
    if (i == LEN) {
      break;
    }
    buf[i] = node_ident(typ_definition_const(t->gen_args[n]));
    i += 1;
  }
#undef LEN

  t->hash = hash32_hsieh(buf, sizeof(buf));
  t->rdy |= RDY_HASH;
}

static ERROR update_quickisa_isalist_each(struct module *mod,
                                          struct typ *t, struct typ *intf,
                                          bool *stop, void *user) {
  typset_add(&t->quickisa, intf);

  struct typ *intf0 = typ_generic_functor(intf);
  if (intf0 != NULL) {
    typset_add(&t->quickisa, intf0);
  }

  return 0;
}

void typ_create_update_quickisa(struct typ *t) {
  quickisa_init(t);

  error never = typ_isalist_foreach(NULL, t, 0, update_quickisa_isalist_each, NULL);
  assert(!never);

  struct typ *t0 = typ_generic_functor(t);
  if (t0 != NULL) {
    error never = update_quickisa_isalist_each(NULL, t, t0, NULL, NULL);
    assert(!never);
  }

  t->quickisa.ready = true;
}

bool typ_is_tentative(const struct typ *t) {
  return t->flags & TYPF_TENTATIVE;
}

struct typ *typ_create_genarg(struct typ *t) {
  struct typ *r = calloc(1, sizeof(struct typ));
  r->flags = t->flags | TYPF_GENARG;
  r->definition = t->definition;
  set_typ(&r->gen0, r);
  return r;
}

struct typ *typ_create_tentative_functor(struct typ *t) {
  assert(NM(typ_definition_which(t)) & (NM(DEFINTF) | NM(DEFINCOMPLETE)));
  assert(typ_is_generic_functor(t));
  assert(typ_hash_ready(t));

  if (typ_is_tentative(t)) {
    return t;
  }

  struct typ *r = calloc(1, sizeof(struct typ));
  r->flags = t->flags | TYPF_TENTATIVE;
  r->rdy = t->rdy;
  r->hash = t->hash;
  r->definition = t->definition;

  assert(t->rdy & RDY_GEN);
  r->gen_arity = t->gen_arity;
  overlay_init(r);
  r->gen_args = calloc(t->gen_arity, sizeof(*r->gen_args));
  struct typ *final = typ_definition_which(t) == DEFINTF ? typ_member(t, ID_FINAL) : t;
  set_typ(&r->gen0, r /* tentative functors are their own functor */);
  if (typ_is_genarg(final)) {
    overlay_map(r, &r->gen0, final);
  }
  for (size_t n = 0; n < t->gen_arity; ++n) {
    set_typ(&r->gen_args[n], t->gen_args[n]);
    overlay_map(r, &r->gen_args[n], t->gen_args[n]);
  }

  typ_create_update_quickisa(r);

  return r;
}

struct typ *typ_create_tentative(struct typ *t, struct typ **args, size_t arity) {
  assert(typ_hash_ready(t));
  assert(t->gen_arity == arity);

  struct typ *r = calloc(1, sizeof(struct typ));
  r->flags = t->flags | TYPF_TENTATIVE;
  r->rdy = t->rdy;
  assert(t->rdy & RDY_HASH);
  r->hash = t->hash;
  r->definition = t->definition;

  assert(t->rdy & RDY_GEN);
  r->gen_arity = t->gen_arity;
  overlay_init(r);
  r->gen_args = calloc(t->gen_arity, sizeof(*r->gen_args));
  struct typ *final = typ_definition_which(t) == DEFINTF ? typ_member(t, ID_FINAL) : t;
  set_typ(&r->gen0, final);
  if (typ_is_genarg(final)) {
    overlay_map(r, &r->gen0, final);
  }
  for (size_t n = 0; n < t->gen_arity; ++n) {
    set_typ(&r->gen_args[n], args[n]);
    overlay_map(r, &r->gen_args[n], t->gen_args[n]);
  }

  typ_create_update_quickisa(r);

  return r;
}

static bool is_actually_still_tentative(const struct typ *user) {
  const struct node *dpar = parent_const(typ_definition_const(user));
  if ((NM(dpar->which) & (NM(DEFTYPE) | NM(DEFINTF))) && typ_is_tentative(dpar->typ)) {
    return true;
  }
  if (typ_generic_arity(user) > 0) {
    if (typ_is_tentative(typ_generic_functor_const(user))) {
      return true;
    }
    for (size_t n = 0, arity = typ_generic_arity(user); n < arity; ++n) {
      if (typ_is_tentative(typ_generic_arg_const(user, n))) {
        return true;
      }
    }
  }
  return false;
}

static void link_finalize(struct typ *user) {
  if (typ_was_zeroed(user)) {
    // Was zeroed in link_generic_functor_update().
    return;
  }

  if (typ_is_tentative(user) && is_actually_still_tentative(user)) {
    return;
  }

  schedule_finalization(user);
}

static void link_generic_functor_update(struct typ *user, struct typ *dst, struct typ *src) {
  if (typ_was_zeroed(user)) {
    return;
  }

  assert(typ_is_tentative(user));

  struct typ *user0 = typ_generic_functor(user);
  if (user0 == src) {
    // 'src' was used by 'user' as generic functor. Linking to the new
    // functor means creating a new instance, and linking 'user' to it.

    assert(typ_is_generic_functor(src));
    assert(typ_is_generic_functor(dst));

    struct module *trigger_mod = node_toplevel_const(typ_definition_const(user))
      ->generic->trigger_mod;
    const bool need_state = !trigger_mod->state->tentatively
      && trigger_mod->state->top_state == NULL;
    if (need_state) {
      PUSH_STATE(trigger_mod->state);
      trigger_mod->state->tentatively = true;
    }

    struct typ *new_user = unify_with_new_functor(trigger_mod, NULL, dst, user);
    assert(typ_was_zeroed(user) && "must have been zeroed");

    if (need_state) {
      POP_STATE(trigger_mod->state);
    }

    typ_create_update_hash(new_user);
    create_update_concrete_flag(new_user);
  }
}

static void link_generic_arg_update(struct typ *user, struct typ *dst, struct typ *src) {
  if (typ_was_zeroed(user)) {
    return;
  }

  assert(typ_is_tentative(user));
  assert(!typ_is_generic_functor(src));

  // If 'src' was used by 'user' as a generic arg, then the
  // FOREACH_BACKLINK() pass already updated the SETGENARG in
  // typ_definition(user).

  for (size_t n = 0, arity = typ_generic_arity(user); n < arity; ++n) {
    struct typ *ga = typ_generic_arg(user, n);
    if (ga == dst) {
      // We do have to do it by hand, add_user() doesn't use set_typ().
      add_user(dst, user);
    }
  }

  typ_create_update_hash(user);
  create_update_concrete_flag(user);
}

static void link_parent_update(struct typ *user, struct typ *dst, struct typ *src) {
  if (typ_was_zeroed(user)) {
    return;
  }
  if (typ_is_generic_functor(user)) {
    return;
  }

  assert(typ_is_tentative(user));

  struct node *dpar = parent(typ_definition(user));
  if (dpar->typ == src) {
    struct module *trigger_mod = node_toplevel_const(typ_definition_const(user))
      ->generic->trigger_mod;

    unify_with_new_parent(trigger_mod, NULL, dst, user);
  }
}

static void link_to_final(struct module *mod, struct typ *dst, struct typ *src) {
  FOREACH_USER(idx, user, src, link_parent_update(user, dst, src));

  FOREACH_USER(idx, user, src, link_generic_functor_update(user, dst, src));

  FOREACH_BACKLINK(idx, back, src, unset_typ(back); set_typ(back, dst));

  FOREACH_USER(idx, user, src, link_generic_arg_update(user, dst, src));

  FOREACH_USER(idx, user, src, link_finalize(user));

  // Noone should be referring to 'src' anymore, except gen0; let's make sure.
  struct typ *gen0 = src->gen0;
  memset(src, 0, sizeof(*src));
  src->gen0 = gen0;
}

// When linking src to a tentative dst, and when src is a generic, each of
// the generic arguments of src have gained a user (dst) and lost a user
// (src).
noinline__ // Expensive -- we want it to show up in profiles.
static void remove_as_user_of_generic_args(struct typ *t) {
  for (size_t n = 0, arity = typ_generic_arity(t); n < arity; ++n) {
    struct typ *arg = typ_generic_arg(t, n);
    FOREACH_USER(idx, user, arg,
                 if (user == t) { users->users[idx] = NULL; });
  }
}

static void link_to_tentative(struct module *mod, struct typ *dst, struct typ *src) {
  FOREACH_USER(idx, user, src, link_parent_update(user, dst, src));

  FOREACH_USER(idx, user, src, link_generic_functor_update(user, dst, src));

  FOREACH_BACKLINK(idx, back, src, unset_typ(back); set_typ(back, dst));

  FOREACH_USER(idx, user, src, link_generic_arg_update(user, dst, src));

  remove_as_user_of_generic_args(src);

  clear_backlinks(src);
  clear_users(src);

  // Noone should be referring to 'src' anymore, except gen0; let's make sure.
  struct typ *gen0 = src->gen0;
  memset(src, 0, sizeof(*src));
  src->gen0 = gen0;
}

void typ_link_tentative(struct typ *dst, struct typ *src) {
  assert(typ_is_tentative(src));
  assert(!typ_is_generic_functor(src) && "use typ_link_tentative_functor()");

  if (dst == src) {
    return;
  }

  if (!typ_is_tentative(dst)) {
    link_to_final(NULL, dst, src);
  } else {
    link_to_tentative(NULL, dst, src);
  }
}

void typ_link_tentative_functor(struct module *mod, struct typ *dst, struct typ *src) {
  assert(mod != NULL);
  assert(typ_is_tentative(src));
  assert(typ_is_generic_functor(src));
  assert(typ_is_generic_functor(dst));

  if (dst == src) {
    return;
  }

  if (!typ_is_tentative(dst)) {
    link_to_final(NULL, dst, src);
  } else {
    link_to_tentative(NULL, dst, src);
  }
}

void typ_link_to_existing_final(struct typ *dst, struct typ *src) {
  assert(!typ_is_tentative(dst));
  assert(typ_is_tentative(src));

  if (dst == src) {
    return;
  }

  link_to_final(NULL, dst, src);
}

void typ_debug_check_in_backlinks(struct typ **u) {
  struct typ *t = *u;
  if (t == NULL || !typ_is_tentative(t)) {
    return;
  }
  bool r = false;
  FOREACH_BACKLINK(idx, b, t, if (b != NULL) { r |= b == u; });
}

struct typ **typ_permanent_loc(struct typ *t) {
  assert(t->overlay != NULL || !typ_hash_ready(t) || t->definition->which == DEFINCOMPLETE);
  return &t->gen0;
}

struct node *typ_definition_nooverlay(struct typ *t) {
  assert(t->overlay == NULL);
  return t->definition;
}

const struct node *typ_definition_nooverlay_const(const struct typ *t) {
  assert(t->overlay == NULL);
  return t->definition;
}

struct node *typ_definition_ignore_any_overlay(struct typ *t) {
  return t->definition;
}

const struct node *typ_definition_ignore_any_overlay_const(const struct typ *t) {
  return t->definition;
}

const struct node *typ_for_error(const struct typ *t) {
  return node_toplevel_const(t->definition)->generic->for_error;
}

bool typ_is_function(const struct typ *t) {
  const struct node *def = typ_definition_const(t);
  return def->which == DEFFUN || def->which == DEFMETHOD;
}

uint32_t typ_toplevel_flags(const struct typ *t) {
  return node_toplevel_const(typ_definition_const(t))->flags;
}

enum node_which typ_definition_which(const struct typ *t) {
  return typ_definition_const(t)->which;
}

enum deftype_kind typ_definition_deftype_kind(const struct typ *t) {
  const struct node *d = typ_definition_const(t);
  assert(d->which == DEFTYPE);
  return d->as.DEFTYPE.kind;
}

struct typ *typ_definition_tag_type(const struct typ *t) {
  const struct node *d = typ_definition_const(t);
  assert(d->which == DEFTYPE);
  return d->as.DEFTYPE.tag_typ;
}

enum token_type typ_definition_defmethod_access(const struct typ *t) {
  const struct node *d = typ_definition_const(t);
  assert(d->which == DEFMETHOD);
  return d->as.DEFMETHOD.access;
}

struct typ *typ_definition_defmethod_self_wildcard_functor(const struct typ *t) {
  assert(t->definition->which == DEFMETHOD);
  const struct node *genargs = subs_at_const(typ_definition_const(t), IDX_GENARGS);
  return subs_at_const(genargs, t->definition->as.DEFMETHOD.first_wildcard_genarg)->typ;
}

ident typ_definition_ident(const struct typ *t) {
  return node_ident(typ_definition_const(t));
}

struct module *typ_module_owner(const struct typ *t) {
  if (t->definition->which == DEFINCOMPLETE) {
    return t->definition->as.DEFINCOMPLETE.trigger_mod;
  }
  return node_module_owner(CONST_CAST(t)->definition);
}

struct typ *typ_member(struct typ *t, ident name) {
  struct node *d = typ_definition(t);
  struct node *m = node_get_member(d, name);
  return m != NULL ? m->typ : NULL;
}

struct tit {
  struct node *definition;
  struct node *pos;
  uint64_t node_filter;
  bool just_one;
};

bool tit_next(struct tit *tit) {
  struct node *pos = tit->pos;
  if (pos == NULL) {
    // We're being called on a brand new tit.
    pos = subs_first(tit->definition);
  }

  tit->pos = NULL;

  if (tit->just_one) {
    goto done;
  }

  if (pos->which == DEFCHOICE) {
    struct node *f = subs_first(pos);
    if (f != NULL) {
      tit->pos = f;
      goto done;
    }
  }

  struct node *n = next(pos);
  if (n != NULL) {
    tit->pos = n;
    goto done;
  }

  struct node *p = parent(pos);
  if (p->which == DEFCHOICE) {
    tit->pos = next(p);
    goto done;
  }

done:
  if (tit->pos == NULL) {
    free(tit);
    memset(tit, 0, sizeof(*tit));
    return false;
  }

  if (tit->node_filter != 0 && (tit->node_filter & NM(tit->pos->which))) {
    return true;
  }

  return tit_next(tit);
}

struct tit *typ_definition_one_member(const struct typ *t, ident name) {
  struct tit *tit = calloc(1, sizeof(struct tit));
  tit->definition = typ_definition(CONST_CAST(t));
  tit->just_one = true;

  error e = scope_lookup_ident_immediate(&tit->pos, tit->definition,
                                         typ_module_owner(t),
                                         &tit->definition->scope,
                                         name, true);
  if (e) {
    free(tit);
    return NULL;
  }

  return tit;
}

struct tit *typ_definition_members(const struct typ *t, ...) {
  struct tit *tit = calloc(1, sizeof(struct tit));
  tit->definition = typ_definition(CONST_CAST(t));

  va_list ap;
  va_start(ap, t);

  uint64_t f = 0;
  enum node_which n;
  while ((n = va_arg(ap, enum node_which)) != 0) {
    f |= NM(n);
  }
  tit->node_filter = f;

  return tit;
}

// TODO: Ideally, we shouldn't be setting any 'typ' from within types.c. This
// should be coded in inference.c. The next 2 functions have that kind of
// side effect, however.
//

static void bin_accessor_maybe_literal_slice__has_effect(struct module *mod, struct node *par) {
  if (typ_isa(par->typ, TBI_LITERALS_SLICE)) {
    error ok = unify(mod, par, typ_generic_arg(par->typ, 0), TBI_SLICE);
    assert(!ok);
  }
}

static void bin_accessor_maybe_functor__has_effect(struct module *mod, struct node *par) {
  // Something like the (hypothetical): vector.mk_filled 100 0:u8
  // 'vector' is a generic functor, and the instantiation will be done
  // through the call to the function 'vector.mk_filled'. We need to have a
  // fully tentative instance of 'vector' so that the unification of '0:u8'
  // with t:`copyable succeeds.
  if ((par->flags & NODE_IS_TYPE)
      && typ_is_generic_functor(par->typ)
      && node_ident(par) != ID_THIS) {

    struct typ *i = instantiate_fully_implicit(mod, par, par->typ);
    unset_typ(&par->typ);
    set_typ(&par->typ, i);
  }
}

static bool bin_accessor_maybe_ref(struct node **parent_scope,
                                   struct module *mod, struct node *par) {
  if (typ_is_reference(par->typ)) {
    *parent_scope = typ_definition(typ_generic_arg(par->typ, 0));
    return true;
  }
  return false;
}

static void bin_accessor_maybe_defchoice(struct node **parent_scope, struct node *for_error,
                                         struct module *mod, struct node *par) {
  if (par->flags & NODE_IS_DEFCHOICE) {
    struct node *defchoice = NULL;
    error e = scope_lookup_ident_immediate(&defchoice, for_error, mod,
                                           &typ_definition(par->typ)->scope,
                                           node_ident(subs_last(par)), false);
    assert(!e);
    assert(defchoice->which == DEFCHOICE);

    const struct node *ext = node_defchoice_external_payload(defchoice);
    *parent_scope = ext != NULL ? typ_definition(ext->typ) : defchoice;
  }
}

struct tit *typ_resolve_accessor__has_effect(error *e,
                                             struct module *mod,
                                             struct node *node) {
  assert(node->which == BIN && OP_KIND(node->as.BIN.operator) == OP_BIN_ACC);

  struct node *left = subs_first(node);
  bin_accessor_maybe_literal_slice__has_effect(mod, left);
  bin_accessor_maybe_functor__has_effect(mod, left);

  struct node *dcontainer = typ_definition(left->typ);
  if (!bin_accessor_maybe_ref(&dcontainer, mod, left)) {
    bin_accessor_maybe_defchoice(&dcontainer, node, mod, left);
  }
  struct scope *container_scope = &dcontainer->scope;
  const bool container_is_tentative = typ_is_tentative(scope_node(container_scope)->typ);

  struct node *name = subs_last(node);
  struct node *field = NULL;
  *e = scope_lookup_ident_immediate(&field, name, mod, container_scope,
                                    node_ident(name), container_is_tentative);

  if (field->which == IMPORT && !field->as.IMPORT.intermediate_mark) {
    error none = scope_lookup(&field, mod, &mod->gctx->modules_root.scope,
                              subs_first(field), false);
    assert(!none);
  }

  struct tit *r = calloc(1, sizeof(struct tit));
  r->definition = dcontainer;
  r->just_one = true;
  r->pos = field;
  return r;
}

enum node_which tit_which(const struct tit *tit) {
  return tit->pos->which;
}

ident tit_ident(const struct tit *tit) {
  return node_ident(tit->pos);
}

struct typ *tit_typ(const struct tit *tit) {
  return tit->pos->typ;
}

struct typ *tit_parent_definition_typ(const struct tit *tit) {
  struct node *p = tit->pos;
  if (NM(p->which) & (NM(MODULE) | NM(MODULE_BODY) | NM(IMPORT))) {
    return parent(p)->typ;
  }

  while (!node_is_at_top(p)) {
    p = parent(p);
  }
  return p->typ;
}

uint32_t tit_node_flags(const struct tit *tit) {
  return tit->pos->flags;
}

struct tit *tit_let_def(const struct tit *tit) {
  assert(tit->pos->which == LET);
  struct tit *r = calloc(1, sizeof(struct tit));
  r->definition = tit->definition;
  r->just_one = true;
  r->pos = subs_first(tit->pos);
  assert(NM(r->pos->which) & (NM(DEFNAME) | NM(DEFALIAS)));
  return r;
}

bool tit_defchoice_is_leaf(const struct tit *tit) {
  assert(tit->pos->which == DEFCHOICE);
  return tit->pos->as.DEFCHOICE.is_leaf;
}

bool tit_defchoice_is_external_payload(const struct tit *tit) {
  assert(tit->pos->which == DEFCHOICE);
  return node_defchoice_external_payload(tit->pos) != NULL;
}

struct tit *tit_defchoice_lookup_field(const struct tit *variant, ident name) {
  const struct node *d = variant->pos;
  assert(d->which == DEFCHOICE);

  struct node *field = NULL;
  while (true) {
    const struct scope *sc = &d->scope;
    if (d->which == DEFCHOICE) {
      const struct node *ext = node_defchoice_external_payload(d);
      if (ext != NULL) {
        sc = &typ_definition_const(ext->typ)->scope;
      }
    }

    error e = scope_lookup_ident_immediate(&field, NULL, NULL, sc,
                                           name, true);
    if (!e) {
      break;
    }

    if (d->which == DEFTYPE) {
      return NULL;
    }

    d = parent_const(d);
  }

  struct tit *r = calloc(1, sizeof(struct tit));
  r->definition = variant->definition;
  r->just_one = true;
  r->pos = field;
  return r;
}


static struct typ *def_generic_functor(struct typ *t) {
  if (typ_generic_arity(t) == 0) {
    return NULL;
  }

  const struct toplevel *toplevel = node_toplevel_const(typ_definition_const(t));
  if (toplevel->generic->our_generic_functor_typ != NULL) {
    return toplevel->generic->our_generic_functor_typ;
  } else {
    return t;
  }
}

struct node *tit_node_ignore_any_overlay(const struct tit *tit) {
  return tit->pos;
}

const struct node *tit_for_error(const struct tit *tit) {
  return tit_node_ignore_any_overlay(tit);
}

struct typ *typ_generic_functor(struct typ *t) {
  if (t->rdy & RDY_GEN) {
    if (t->gen_arity == 0) {
      return NULL;
    } else {
      return t->gen0;
    }
    return t->gen0;
  } else {
    return def_generic_functor(t);
  }
}

EXAMPLE_NCC(typ_generic_functor) {
//  struct node *test = mock_deftype(mod, "test");
//  struct typ ttest = { 0 };
//  ttest.definition = test;
//  assert(typ_generic_functor(&ttest) == NULL);
}

static size_t def_generic_arity(const struct typ *t) {
  const struct node *d = typ_definition_const(t);
  if (node_can_have_genargs(d)) {
    return subs_count(subs_at_const(d, IDX_GENARGS));
  } else {
    return 0;
  }
}

size_t typ_generic_arity(const struct typ *t) {
  if (t->rdy & RDY_GEN) {
    return t->gen_arity;
  } else {
    return def_generic_arity(t);
  }
}

EXAMPLE_NCC(typ_generic_arity) {
//  {
//    struct node *test = mock_deftype(mod, "test");
//    struct typ ttest = { 0 };
//    ttest.definition = test;
//    assert(typ_generic_arity(&ttest) == 0);
//  }
//  {
//    struct node *test = mock_deftype(mod, "test2");
//    struct node *genargs = test->subs[IDX_GENARGS];
//    G(g1, genargs, DEFGENARG,
//       G_IDENT(name_g1, g1, "g1"));
//    G(g2, genargs, DEFGENARG,
//       G_IDENT(name_g2, g2, "g2"));
//
//    struct typ ttest = { 0 };
//    ttest.definition = test;
//    assert(typ_generic_arity(&ttest) == 2);
//  }
}

size_t typ_generic_first_explicit_arg(const struct typ *t) {
  return node_toplevel_const(typ_definition_const(t))
    ->generic->first_explicit_genarg;
}

static struct typ *def_generic_arg(struct typ *t, size_t n) {
  assert(n < typ_generic_arity(t));
  return subs_at_const(subs_at_const(typ_definition_const(t), IDX_GENARGS), n)->typ;
}

struct typ *typ_generic_arg(struct typ *t, size_t n) {
  if (t->rdy & RDY_GEN) {
    assert(n < t->gen_arity);
    return t->gen_args[n];
  } else {
    return def_generic_arg(t, n);
  }
}

struct typ *typ_as_non_tentative(const struct typ *t) {
  assert(typ_generic_arity(t) == 0 && "FIXME not supported");
  const struct toplevel *toplevel = node_toplevel_const(typ_definition_const(t));
  return toplevel->generic->our_generic_functor_typ;
}

size_t typ_function_arity(const struct typ *t) {
  assert(typ_is_function(t));
  return node_fun_all_args_count(typ_definition_const(t));
}

size_t typ_function_min_arity(const struct typ *t) {
  assert(typ_is_function(t));
  return node_fun_min_args_count(typ_definition_const(t));
}

size_t typ_function_max_arity(const struct typ *t) {
  assert(typ_is_function(t));
  return node_fun_max_args_count(typ_definition_const(t));
}

ssize_t typ_function_first_vararg(const struct typ *t) {
  assert(typ_is_function(t));
  return node_fun_first_vararg(typ_definition_const(t));
}

struct typ *typ_function_arg(struct typ *t, size_t n) {
  assert(n < typ_function_arity(t));
  return subs_at_const(subs_at_const(typ_definition_const(t), IDX_FUNARGS), n)->typ;
}

ident typ_function_arg_ident(const struct typ *t, size_t n) {
  assert(n < typ_function_arity(t));
  return node_ident(subs_at_const(subs_at_const(typ_definition_const(t), IDX_FUNARGS), n));
}

enum token_type typ_function_arg_explicit_ref(const struct typ *t, size_t n) {
  assert(n < typ_function_arity(t));
  const struct node *dfun = t->definition;
  const struct node *funargs = subs_at_const(dfun, IDX_FUNARGS);
  const struct node *darg = subs_at_const(funargs, n);
  if (subs_last_const(darg)->which == UN) {
    return subs_last_const(darg)->as.UN.operator;
  }
  return 0;
}

struct typ *typ_function_return(struct typ *t) {
  assert(typ_definition_const(t)->which == DEFFUN
         || typ_definition_const(t)->which == DEFMETHOD);
  return node_fun_retval_const(typ_definition_const(t))->typ;
}

const struct typ *typ_generic_functor_const(const struct typ *t) {
  return typ_generic_functor(CONST_CAST(t));
}

const struct typ *typ_generic_arg_const(const struct typ *t, size_t n) {
  return typ_generic_arg(CONST_CAST(t), n);
}

const struct typ *typ_function_arg_const(const struct typ *t, size_t n) {
  return typ_function_arg(CONST_CAST(t), n);
}

const struct typ *typ_function_return_const(const struct typ *t) {
  return typ_function_return(CONST_CAST(t));
}

static size_t direct_isalist_count(const struct typ *t) {
  const struct node *def = typ_definition_const(t);
  switch (def->which) {
  case DEFTYPE:
  case DEFINTF:
  case DEFINCOMPLETE:
    return subs_count(subs_at_const(def, IDX_ISALIST));
  default:
    return 0;
  }
}

static struct typ *direct_isalist(struct typ *t, size_t n) {
  const struct node *def = typ_definition_const(t);
  switch (def->which) {
  case DEFTYPE:
  case DEFINTF:
  case DEFINCOMPLETE:
    return subs_at_const(subs_at_const(def, IDX_ISALIST), n)->typ;
  default:
    assert(false);
    return 0;
  }
}

static const struct typ *direct_isalist_const(const struct typ *t, size_t n) {
  return direct_isalist(CONST_CAST(t), n);
}

static bool direct_isalist_exported(const struct typ *t, size_t n) {
  const struct node *def = typ_definition_const(t);
  switch (def->which) {
  case DEFTYPE:
  case DEFINTF:
  case DEFINCOMPLETE:
    return subs_at_const(subs_at_const(def, IDX_ISALIST), n)->as.ISA.is_export;
  default:
    return 0;
  }
}

static ERROR do_typ_isalist_foreach(struct module *mod, struct typ *t, struct typ *base,
                                    uint32_t filter, isalist_each iter, void *user,
                                    bool *stop, struct fintypset *set) {
  const bool filter_not_exported = filter & ISALIST_FILTEROUT_NOT_EXPORTED;
  const bool filter_exported = filter & ISALIST_FILTEROUT_EXPORTED;
  const bool filter_trivial_isalist = filter & ISALIST_FILTEROUT_TRIVIAL_ISALIST;
  bool filter_nontrivial_isalist = filter & ISALIST_FILTEROUT_NONTRIVIAL_ISALIST;
  const bool filter_prevent_dyn = filter & ISALIST_FILTEROUT_PREVENT_DYN;

  if (filter_trivial_isalist && typ_is_trivial(base)) {
    return 0;
  }
  if (filter_nontrivial_isalist && !typ_is_trivial(base)) {
    return 0;
  }
  if (filter_prevent_dyn && typ_isa(base, TBI_PREVENT_DYN)) {
    return 0;
  }

  if (typ_is_trivial(base)) {
    // Trivial interfaces are often empty but refer to definitions in
    // nontrivial interfaces.
    filter &= ~ISALIST_FILTEROUT_NONTRIVIAL_ISALIST;
  }

  filter_nontrivial_isalist = filter & ISALIST_FILTEROUT_NONTRIVIAL_ISALIST;

  for (size_t n = 0; n < direct_isalist_count(base); ++n) {
    struct typ *intf = direct_isalist(base, n);
    if (fintypset_get(set, intf) != NULL) {
      continue;
    }

    if (filter_prevent_dyn && typ_equal(intf, TBI_PREVENT_DYN)) {
      return 0;
    }

    const bool exported = direct_isalist_exported(base, n);
    if (filter_not_exported && !exported) {
      continue;
    }
    if (filter_exported && exported) {
      continue;
    }
    if (filter_trivial_isalist && typ_is_trivial(intf)) {
      continue;
    }
    if (filter_nontrivial_isalist && !typ_is_trivial(intf)) {
      continue;
    }

    if (exported) {
      filter &= ~ISALIST_FILTEROUT_NOT_EXPORTED;
    } else {
      filter &= ~ISALIST_FILTEROUT_EXPORTED;
    }

    fintypset_set(set, intf, true);

    error e = do_typ_isalist_foreach(mod, t, intf, filter, iter, user, stop, set);
    EXCEPT(e);

    if (*stop) {
      return 0;
    }

    e = iter(mod, t, intf, stop, user);
    EXCEPT(e);

    if (*stop) {
      return 0;
    }
  }

  return 0;
}

error typ_isalist_foreach(struct module *mod, struct typ *t, uint32_t filter,
                          isalist_each iter, void *user) {
  bool stop = false;
  struct fintypset set;
  fintypset_fullinit(&set);

  const bool is_export = node_toplevel_const(typ_definition_const(t))->flags & TOP_IS_EXPORT;
  const bool filter_exported = filter & ISALIST_FILTEROUT_EXPORTED;
  const bool filter_not_exported = filter & ISALIST_FILTEROUT_NOT_EXPORTED;
  if (!(filter_exported && is_export) && !(filter_not_exported && !is_export)) {
    error e = iter(mod, t, TBI_ANY, &stop, user);
    EXCEPT(e);

    if (stop) {
      return 0;
    }
    fintypset_set(&set, TBI_ANY, true);
  }

  error e = do_typ_isalist_foreach(mod, t, t, filter, iter, user, &stop, &set);
  fintypset_destroy(&set);
  EXCEPT(e);

  return 0;
}

EXAMPLE_NCC(direct_isalist_foreach) {
//  struct node *i_test = mock_defintf(mod, "`test");
//  struct node *test = mock_deftype(mod, "test");
//  G(isa, test->subs[IDX_ISALIST], ISA,
//     G_IDENT(isa_name, isa, "`test"));
//
//  i_test->typ = typ_create(NULL, i_test);
//  test->typ = typ_create(NULL, test);
//  set_typ(&isa_name->typ, i_test->typ);
//  set_typ(&isa->typ, isa_name->typ);
//
//  int count = 0;
//  error e = typ_isalist_foreach(mod, test->typ, 0, example_isalist_each, &count);
//  assert(!e);
//  assert(count == 1);
}

struct typ *TBI_VOID;
struct typ *TBI_LITERALS_NULL;
struct typ *TBI_LITERALS_INTEGER;
struct typ *TBI_LITERALS_FLOATING;
struct typ *TBI_LITERALS_SLICE;
struct typ *TBI_ANY_TUPLE;
struct typ *TBI_TUPLE_2;
struct typ *TBI_TUPLE_3;
struct typ *TBI_TUPLE_4;
struct typ *TBI_TUPLE_5;
struct typ *TBI_TUPLE_6;
struct typ *TBI_TUPLE_7;
struct typ *TBI_TUPLE_8;
struct typ *TBI_TUPLE_9;
struct typ *TBI_TUPLE_10;
struct typ *TBI_TUPLE_11;
struct typ *TBI_TUPLE_12;
struct typ *TBI_TUPLE_13;
struct typ *TBI_TUPLE_14;
struct typ *TBI_TUPLE_15;
struct typ *TBI_TUPLE_16;
struct typ *TBI_ANY;
struct typ *TBI_BOOL;
struct typ *TBI_BOOL_COMPATIBLE;
struct typ *TBI_I8;
struct typ *TBI_U8;
struct typ *TBI_I16;
struct typ *TBI_U16;
struct typ *TBI_I32;
struct typ *TBI_U32;
struct typ *TBI_I64;
struct typ *TBI_U64;
struct typ *TBI_UINT;
struct typ *TBI_INT;
struct typ *TBI_UINTPTR;
struct typ *TBI_INTPTR;
struct typ *TBI_FLOAT;
struct typ *TBI_DOUBLE;
struct typ *TBI_CHAR;
struct typ *TBI_STRING;
struct typ *TBI_STRING_COMPATIBLE;
struct typ *TBI_STATIC_ARRAY;
struct typ *TBI_ANY_ANY_REF;
struct typ *TBI_ANY_REF;
struct typ *TBI_ANY_MUTABLE_REF;
struct typ *TBI_ANY_NULLABLE_REF;
struct typ *TBI_ANY_NULLABLE_MUTABLE_REF;
struct typ *TBI_REF; // @
struct typ *TBI_MREF; // @!
struct typ *TBI_MMREF; // @#
struct typ *TBI_NREF; // ?@
struct typ *TBI_NMREF; // ?@!
struct typ *TBI_NMMREF; // ?@#
struct typ *TBI_VOIDREF;
struct typ *TBI_ANY_ANY_SLICE;
struct typ *TBI_ANY_SLICE;
struct typ *TBI_ANY_MUTABLE_SLICE;
struct typ *TBI_SLICE;
struct typ *TBI_MSLICE;
struct typ *TBI_SLICE_IMPL;
struct typ *TBI_SLICE_COMPATIBLE;
struct typ *TBI_VARARG;
struct typ *TBI_ARITHMETIC;
struct typ *TBI_INTEGER_ARITHMETIC;
struct typ *TBI_OVERFLOW_ARITHMETIC;
struct typ *TBI_INTEGER_LITERAL_COMPATIBLE;
struct typ *TBI_INTEGER;
struct typ *TBI_UNSIGNED_INTEGER;
struct typ *TBI_NATIVE_INTEGER;
struct typ *TBI_NATIVE_SIZED_UNSIGNED_INTEGER;
struct typ *TBI_GENERALIZED_BOOLEAN;
struct typ *TBI_NATIVE_BOOLEAN;
struct typ *TBI_FLOATING;
struct typ *TBI_NATIVE_FLOATING;
struct typ *TBI_HAS_EQUALITY;
struct typ *TBI_NOT_HAS_EQUALITY;
struct typ *TBI_ORDERED;
struct typ *TBI_NOT_ORDERED;
struct typ *TBI_EQUALITY_BY_COMPARE;
struct typ *TBI_ORDERED_BY_COMPARE;
struct typ *TBI_COPYABLE;
struct typ *TBI_NOT_COPYABLE;
struct typ *TBI_DEFAULT_CTOR;
struct typ *TBI_NON_DEFAULT_CTOR;
struct typ *TBI_DEFAULT_DTOR;
struct typ *TBI_ARRAY_CTOR;
struct typ *TBI_TRIVIAL_COPY;
struct typ *TBI_TRIVIAL_COPY_BUT_OWNED;
struct typ *TBI_TRIVIAL_CTOR;
struct typ *TBI_TRIVIAL_ARRAY_CTOR;
struct typ *TBI_TRIVIAL_DTOR;
struct typ *TBI_TRIVIAL_COMPARE;
struct typ *TBI_TRIVIAL_EQUALITY;
struct typ *TBI_TRIVIAL_ORDER;
struct typ *TBI_RETURN_BY_COPY;
struct typ *TBI_NOT_RETURN_BY_COPY;
struct typ *TBI_ENUM;
struct typ *TBI_UNION;
struct typ *TBI_UNION_TRIVIAL_CTOR;
struct typ *TBI_RANGE;
struct typ *TBI_BOUNDS;
struct typ *TBI_ITERATOR;
struct typ *TBI_ENVIRONMENT;
struct typ *TBI_ANY_ENVIRONMENT;
struct typ *TBI_PREVENT_DYN;
struct typ *TBI__NOT_TYPEABLE;
struct typ *TBI__CALL_FUNCTION_SLOT;
struct typ *TBI__MUTABLE;
struct typ *TBI__MERCURIAL;

static bool __typ_equal(const struct typ *a, const struct typ *b) {
  const struct node *da = typ_definition_const(a);
  const struct node *db = typ_definition_const(b);
  if (da == db) {
    return true;
  }

  const size_t a_ga = typ_generic_arity(a);
  if (a_ga == 0) {
    const bool a_tentative = typ_is_tentative(a);
    const bool b_tentative = typ_is_tentative(b);
    if (!a_tentative && !b_tentative) {
      return da == db;
    }

    if (a_tentative) {
      da = typ_definition_const(node_toplevel_const(da)->generic->our_generic_functor_typ);
    }
    if (b_tentative) {
      db = typ_definition_const(node_toplevel_const(db)->generic->our_generic_functor_typ);
    }
    return da == db;
  } else {
    if (a_ga != typ_generic_arity(b)) {
      return false;
    }

    const struct typ *a0 = typ_generic_functor_const(a);
    const struct typ *b0 = typ_generic_functor_const(b);
    if (a0 != b0) {
      return false;
    }

    const bool a_functor = typ_is_generic_functor(a);
    const bool b_functor = typ_is_generic_functor(b);
    if (a_functor && b_functor) {
      return true;
    } else if (a_functor || b_functor) {
      return false;
    }

    for (size_t n = 0; n < a_ga; ++n) {
      if (!typ_equal(typ_generic_arg_const(a, n),
                     typ_generic_arg_const(b, n))) {
        return false;
      }
    }

    return true;
  }
}

bool typ_equal(const struct typ *a, const struct typ *b) {
  if (a->hash != b->hash) {
    return false;
  }

  return __typ_equal(a, b);
}

EXAMPLE_NCC(typ_equal) {
//  struct node *di = mock_defintf(mod, "i");
//  struct typ *i = typ_create(NULL, di);
//  di->typ = i;
//
//  struct node *da = mock_deftype(mod, "a");
//  struct node *db = mock_deftype(mod, "b");
//
//  da->typ = typ_create(NULL, da);
//  struct typ *alt_a = typ_create(NULL, da);
//  db->typ = typ_create(NULL, db);
//
//  assert(typ_equal(da->typ, da->typ));
//  assert(typ_equal(da->typ, alt_a));
//  assert(typ_equal(alt_a, da->typ));
//
//  assert(!typ_equal(da->typ, db->typ));
//  assert(!typ_equal(db->typ, da->typ));
//
//  struct node *dc = mock_deftype(mod, "dc");
//  {
//    G(g, dc->subs[IDX_GENARGS], DEFGENARG,
//       G_IDENT(g_name, g, "g");
//       G_IDENT(g_type, g, "i"));
//    dc->typ = typ_create(NULL, dc);
//    g_type->typ = i;
//    g->typ = i;
//    assert(typ_generic_arity(dc->typ) == 1);
//  }
//
//  struct node *dd = mock_deftype(mod, "dd");
//  {
//    G(g, dd->subs[IDX_GENARGS], SETGENARG,
//       G_IDENT(g_name, g, "g");
//       G_IDENT(g_type, g, "i"));
//    dd->typ = typ_create(NULL, dd);
//    node_toplevel(dd)->our_generic_functor_typ = dc->typ;
//    g_type->typ = i;
//    g->typ = i;
//    assert(typ_generic_arity(dd->typ) == 1);
//  }
//
//  assert(typ_equal(typ_generic_functor(dc->typ), typ_generic_functor(dd->typ)));
//  assert(!typ_equal(dc->typ, dd->typ));
}

error typ_check_equal(const struct module *mod, const struct node *for_error,
                      const struct typ *a, const struct typ *b) {
  if (typ_equal(a, b)) {
    return 0;
  }

  error e = 0;
  char *na = pptyp(mod, a);
  char *nb = pptyp(mod, b);
  GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                   "'%s' not equal to type '%s'", na, nb);
except:
  free(nb);
  free(na);
  return e;
}

struct typ *typ_lookup_builtin_tuple(struct module *mod, size_t arity) {
  switch (arity) {
  case 2: return TBI_TUPLE_2;
  case 3: return TBI_TUPLE_3;
  case 4: return TBI_TUPLE_4;
  case 5: return TBI_TUPLE_5;
  case 6: return TBI_TUPLE_6;
  case 7: return TBI_TUPLE_7;
  case 8: return TBI_TUPLE_8;
  case 9: return TBI_TUPLE_9;
  case 10: return TBI_TUPLE_10;
  case 11: return TBI_TUPLE_11;
  case 12: return TBI_TUPLE_12;
  case 13: return TBI_TUPLE_13;
  case 14: return TBI_TUPLE_14;
  case 15: return TBI_TUPLE_15;
  case 16: return TBI_TUPLE_16;
  default: assert(false); return NULL;
  }
}

bool typ_has_same_generic_functor(const struct module *mod,
                                  const struct typ *a, const struct typ *b) {
  const size_t a_arity = typ_generic_arity(a);
  const size_t b_arity = typ_generic_arity(b);

  if (a_arity > 0 && b_arity > 0) {
    return typ_equal(typ_generic_functor_const(a), typ_generic_functor_const(b));
  } else if (a_arity == 0 && b_arity > 0) {
    return typ_equal(a, typ_generic_functor_const(b));
  } else if (a_arity > 0 && b_arity == 0) {
    return typ_equal(typ_generic_functor_const(a), b);
  } else {
    return false;
  }
}

bool typ_is_generic_functor(const struct typ *t) {
  return typ_generic_arity(t) > 0
    && typ_generic_functor_const(t) == t;
}

static bool is_isalist_literal(const struct typ *t) {
  const struct node *d = typ_definition_const(t);
  return d->which == DEFINCOMPLETE && d->as.DEFINCOMPLETE.is_isalist_literal;
}

// Rarely, for certains (a, intf), we don't have enough information in
// quickisa to answer. We could reduce that frequency further by adding more
// information in quickisa.
// There are however cases that are just inherently too numerous to record
// explicitly, such as
//  (Ref I32) isa (`Any_ref `Arithmetic)
// We do record that I32 isa `Arithmetic, though, so the recursive calls to
// typ_isa() don't generally go very deep.
//
// We could also cache the result of typ_isa(). Right now, the overall cost
// of typ_isa() is negligible.
static bool can_use_quickisa(const struct typ *a, const struct typ *intf) {
  return (typ_generic_functor_const(intf) == NULL
          || typ_is_generic_functor(intf)
          || typ_is_concrete(intf))
    && !is_isalist_literal(a) && !is_isalist_literal(intf);
}

bool typ_isa(const struct typ *a, const struct typ *intf) {
  if (typ_equal(intf, TBI_ANY)) {
    return true;
  }

  if (typ_equal(a, intf)) {
    return true;
  }

  if (a->quickisa.ready && can_use_quickisa(a, intf)) {
    return typset_has(&a->quickisa, intf);
  }

  const size_t a_ga = typ_generic_arity(a);
  if (a_ga > 0
      && !typ_is_generic_functor(a)
      && typ_is_generic_functor(intf)) {
    if (typ_isa(typ_generic_functor_const(a), intf)) {
      return true;
    }
  }

  if (a_ga > 0
      && !typ_equal(intf, TBI_ANY_TUPLE)
      && typ_isa(a, TBI_ANY_TUPLE)) {
    // FIXME: only valid for certain builtin interfaces (copy, trivial...)
    size_t n;
    for (n = 0; n < a_ga; ++n) {
      if (!typ_isa(typ_generic_arg_const(a, n), intf)) {
        break;
      }
    }
    if (n == a_ga) {
      return true;
    }
  }

  if (a_ga > 0
      && a_ga == typ_generic_arity(intf)
      && typ_equal(typ_generic_functor_const(a),
                   typ_generic_functor_const(intf))) {
    size_t n = 0;
    for (n = 0; n < a_ga; ++n) {
      if (!typ_isa(typ_generic_arg_const(a, n),
                   typ_generic_arg_const(intf, n))) {
        break;
      }
    }
    if (n == a_ga) {
      return true;
    }
  }

  for (size_t n = 0; n < direct_isalist_count(a); ++n) {
    if (typ_equal(direct_isalist_const(a, n), intf)) {
      return true;
    }
  }

  if (is_isalist_literal(a)) {
    for (size_t n = 0; n < direct_isalist_count(a); ++n) {
      if (typ_isa(direct_isalist_const(a, n), intf)) {
        return true;
      }
    }
    return false;
  }

  if (is_isalist_literal(intf)) {
    for (size_t n = 0; n < direct_isalist_count(intf); ++n) {
      if (!typ_isa(a, direct_isalist_const(intf, n))) {
        return false;
      }
    }
    return true;
  }

  for (size_t n = 0; n < direct_isalist_count(a); ++n) {
    if (typ_isa(direct_isalist_const(a, n), intf)) {
      return true;
    }
  }

  if (typ_is_reference(a) && typ_definition_const(a)->which != DEFINTF) {
    const struct typ *arg = typ_generic_arg_const(a, 0);
    if (typ_definition_const(arg)->which == DEFINTF) {
      return typ_isa(arg, intf);
    }
  }

  return false;
}

error typ_check_isa(const struct module *mod, const struct node *for_error,
                    const struct typ *a, const struct typ *intf) {
  if (typ_isa(a, intf)) {
    return 0;
  }

  error e = 0;
  char *na = pptyp(mod, a);
  char *nintf = pptyp(mod, intf);
  GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                   "'%s' not isa intf '%s'", na, nintf);
except:
  free(nintf);
  free(na);
  return e;
}

bool typ_is_reference(const struct typ *t) {
  return t->flags & TYPF_REF;
}

bool typ_is_nullable_reference(const struct typ *t) {
  return t->flags & TYPF_NREF;
}

bool typ_is_slice(const struct typ *t) {
  return t->flags & TYPF_SLICE;
}

bool typ_is_dyn(const struct typ *t) {
  if (typ_is_tentative(t)
      || !typ_is_reference(t)
      || typ_generic_arity(t) == 0) {
    return false;
  }

  const struct typ *a = typ_generic_arg_const(t, 0);
  return typ_definition_const(a)->which == DEFINTF;
}

bool typ_is_dyn_compatible(const struct typ *t) {
  if (!typ_is_reference(t)
      || typ_generic_arity(t) == 0) {
    return false;
  }

  const struct typ *a = typ_generic_arg_const(t, 0);
  return typ_definition_const(a)->which != DEFINTF;
}

error typ_check_is_reference(const struct module *mod, const struct node *for_error,
                             const struct typ *a) {
  if (typ_is_reference(a)) {
    return 0;
  }

  error e = 0;
  char *na = pptyp(mod, a);
  GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                   "'%s' type is not a reference", na);
except:
  free(na);
  return e;
}

static const char *string_for_ref[TOKEN__NUM] = {
  [TREFDOT] = "@",
  [TREFBANG] = "@!",
  [TREFSHARP] = "@#",
  [TDEREFDOT] = ".",
  [TDEREFBANG] = "!",
  [TDEREFSHARP] = "#",
  [TNULREFDOT] = "?@",
  [TNULREFBANG] = "?@!",
  [TNULREFSHARP] = "?@#",
};

error typ_check_can_deref(const struct module *mod, const struct node *for_error,
                          const struct typ *a, enum token_type operator) {
  error e = 0;
  char *na = NULL;
  if (!typ_is_reference(a)) {
    na = pptyp(mod, a);
    GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                     "'%s' type is not a reference", na);
  }

  if (typ_equal(a, TBI_ANY_ANY_REF)) {
    na = pptyp(mod, a);
    GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                     "cannot dereference type '%s'", na);
  }

  bool ok = false;
  switch (operator) {
  case TDEREFDOT:
  case TDEREFWILDCARD:
    ok = true;
    break;
  case TDEREFBANG:
    ok = typ_isa(a, TBI_ANY_MUTABLE_REF);
    break;
  case TDEREFSHARP:
    ok = typ_has_same_generic_functor(mod, a, TBI_MMREF)
      || typ_has_same_generic_functor(mod, a, TBI_NMMREF);
    break;
  default:
    assert(false);
  }
  if (ok) {
    return 0;
  }

  na = pptyp(mod, a);
  GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                   "'%s' type cannot be dereferenced with '%s'",
                   na, string_for_ref[operator]);
except:
  free(na);
  return e;
}

error typ_check_deref_against_mark(const struct module *mod, const struct node *for_error,
                                   const struct typ *t, enum token_type operator) {
  if (t == NULL) {
    return 0;
  }

  const char *kind = NULL;
  if (typ_equal(t, TBI__MUTABLE)) {
    if (operator != TDOT && operator != TDEREFDOT && operator != TDEREFWILDCARD) {
      return 0;
    }
    kind = "mutable";
  } else if (typ_equal(t, TBI__MERCURIAL)) {
    if (operator == TSHARP || operator == TDEREFSHARP
        || operator == TWILDCARD || operator == TDEREFWILDCARD) {
      return 0;
    }
    kind = "mercurial";
  } else {
    return 0;
  }

  error e = 0;
  char *nt = pptyp(mod, t);
  GOTO_EXCEPT_TYPE(try_node_module_owner_const(mod, for_error), for_error,
                   "'%s' type cannot be dereferenced with '%s' in a %s context",
                   nt, token_strings[operator], kind);
except:
  free(nt);
  return e;
}

bool typ_is_builtin(const struct module *mod, const struct typ *t) {
  return t->flags & TYPF_BUILTIN;
}

bool typ_is_pseudo_builtin(const struct typ *t) {
  return t->flags & TYPF_PSEUDO_BUILTIN;
}

bool typ_is_trivial(const struct typ *t) {
  return t->flags & TYPF_TRIVIAL;
}

bool typ_is_literal(const struct typ *t) {
  return t->flags & TYPF_LITERAL;
}

bool typ_is_concrete(const struct typ *t) {
  return t->flags & TYPF_CONCRETE;
}

bool typ_isa_return_by_copy(const struct typ *t) {
  return typ_isa(t, TBI_RETURN_BY_COPY);
}


static uint32_t instance_hash(const struct instance *i) {
  switch (i->which) {
  case INST_ENTRY:
    return i->as.ENTRY->typ->hash;
  case INST_QUERY_FINAL:
  case INST_QUERY_IDENTICAL:
    return i->as.QUERY->hash;
  default:
    assert(false);
    return 0;
  }
}

static int instance_cmp(const struct instance *a, const struct instance *b) {
  if (a->which == INST_ENTRY && b->which == INST_ENTRY) {
    return !typ_equal(a->as.ENTRY->typ, b->as.ENTRY->typ);
  }

  if (a->which == INST_ENTRY) {
    SWAP(a, b);
  }

  struct typ *ta = a->as.QUERY;
  struct typ *tb = b->as.ENTRY->typ;

  switch (a->which) {
  case INST_ENTRY:
    assert(false);
    return 1;
  case INST_QUERY_FINAL:
    if (typ_is_tentative(tb)) {
      return 1;
    }

    for (size_t n = 0; n < typ_generic_arity(tb); ++n) {
      const struct typ *ga = typ_generic_arg_const(ta, n);
      const struct typ *gb = typ_generic_arg_const(tb, n);
      if (!typ_equal(ga, gb)) {
        return 1;
      }
    }
    return 0;
  case INST_QUERY_IDENTICAL:
    // Use pointer comparison: we're looking for the exact same arguments
    // (same tentative bit, etc.).

    if (typ_generic_functor_const(ta) != typ_generic_functor_const(tb)) {
      return 1;
    }
    for (size_t n = 0; n < typ_generic_arity(tb); ++n) {
      const struct typ *ga = typ_generic_arg_const(ta, n);
      const struct typ *gb = typ_generic_arg_const(tb, n);
      if (ga != gb) {
        return 1;
      }
    }
    return 0;
  }

  return 1;
}

IMPLEMENT_HTABLE_SPARSE(, instanceset, struct node *, struct instance,
                        instance_hash, instance_cmp);

void instances_init(struct node *gendef) {
  struct generic *generic = node_toplevel(gendef)->generic;
  instanceset_init(&generic->finals, 0);
}

static void do_instances_add(struct node *gendef, struct node *instance,
                             bool maintaining_tentatives);

void instances_maintain(struct typ *genf) {
  struct node *gendef = typ_definition(genf);
  struct generic *generic = node_toplevel(gendef)->generic;

  // Maintaining invariant: move finals out.
  for (ssize_t n = 0; n < vecnode_count(&generic->tentatives); ++n) {
    struct node **i = vecnode_get(&generic->tentatives, n);
    if (typ_hash_ready((*i)->typ) && !typ_is_tentative((*i)->typ)) {
      do_instances_add(gendef, *i, true);
      n += vecnode_remove_replace_with_last(&generic->tentatives, n);
    }
  }
}

static void do_instances_add(struct node *gendef, struct node *instance,
                             bool maintaining_tentatives) {
  struct generic *generic = node_toplevel(gendef)->generic;

  if (!maintaining_tentatives) {
    instances_maintain(gendef->typ);
  }

  if (!typ_hash_ready(instance->typ) || typ_is_tentative(instance->typ)) {
    vecnode_push(&generic->tentatives, instance);
    return;
  }

  struct instance i = { 0 };
  i.which = INST_ENTRY;
  i.as.ENTRY = instance;

  const bool already = instanceset_set(&generic->finals, i, instance);
  // When maintaining_tentatives, instance->typ has changed -- it was linked
  // -- since we last looked, so maybe its actual final value was already in
  // generic->finals all along.
  assert(maintaining_tentatives || !already);
}

void instances_add(struct typ *genf, struct node *instance) {
  struct node *gendef = typ_definition(genf);
  do_instances_add(gendef, instance, false);
}

static void prepare_query_tmp(struct typ *tmp, struct typ *functor,
                              struct typ **args, size_t arity) {
  tmp->definition = typ_definition(functor);
  tmp->gen_arity = arity;
  tmp->gen_args = calloc(arity, sizeof(*tmp->gen_args));
  // Not using set_typ(), 'tmp' doesn't live long enough to see linking.
  set_typ(&tmp->gen0, functor);
  memcpy(tmp->gen_args, args, arity * sizeof(*tmp->gen_args));
  tmp->rdy = RDY_GEN;

  create_flags(tmp, (functor->flags & TYPF_BUILTIN) ? functor : NULL);

  typ_create_update_hash(tmp);
}

static bool has_null_arg(struct typ **args, size_t arity) {
  for (size_t n = 0; n < arity; ++n) {
    if (args[n] == NULL) {
      return true;
    }
  }
  return false;
}

struct typ *instances_find_existing_final_with(struct typ *genf,
                                               struct typ **args, size_t arity) {
  if (arity == 0) {
    return NULL;
  }
  if (has_null_arg(args, arity)) {
    return NULL;
  }

  struct node *gendef = typ_definition(genf);
  struct typ *functor = typ_generic_functor(gendef->typ);
  assert(arity == typ_generic_arity(gendef->typ));

  if (!typ_hash_ready(functor)) {
    return NULL;
  }

  struct generic *generic = node_toplevel(CONST_CAST(gendef))->generic;

  instances_maintain(genf);

  struct typ tmp = { 0 };
  prepare_query_tmp(&tmp, functor, args, arity);

  struct instance q = { 0 };
  q.which = INST_QUERY_FINAL;
  q.as.QUERY = &tmp;

  struct node **i = instanceset_get(&generic->finals, q);
  free(tmp.gen_args);
  unset_typ(&tmp.gen0);

  return i == NULL ? NULL : (*i)->typ;
}

struct typ *instances_find_existing_final_like(const struct typ *_t) {
  struct typ *t = CONST_CAST(_t);
  if (!typ_hash_ready(t)) {
    return NULL;
  }

  struct node *gendef = typ_definition(t);
  struct generic *generic = node_toplevel(CONST_CAST(gendef))->generic;

  instances_maintain(t);

  struct instance q = { 0 };
  q.which = INST_QUERY_FINAL;
  q.as.QUERY = t;

  struct node **i = instanceset_get(&generic->finals, q);
  return i == NULL ? NULL : (*i)->typ;
}

struct typ *instances_find_existing_identical(struct typ *functor,
                                              struct typ **args, size_t arity) {
  if (has_null_arg(args, arity)) {
    return NULL;
  }

  struct node *gendef = typ_definition(functor);
  struct generic *generic = node_toplevel(CONST_CAST(gendef))->generic;

  struct typ tmp = { 0 };
  prepare_query_tmp(&tmp, functor, args, arity);

  struct instance q = { 0 };
  q.which = INST_QUERY_IDENTICAL;
  q.as.QUERY = &tmp;

  struct typ *ret = NULL;
  for (ssize_t n = 0; n < vecnode_count(&generic->tentatives); ++n) {
    struct node **i = vecnode_get(&generic->tentatives, n);
    if (typ_hash_ready((*i)->typ) && !typ_is_tentative((*i)->typ)) {
      // Also maintaining invariant: move finals out.
      do_instances_add(gendef, *i, true);
      n += vecnode_remove_replace_with_last(&generic->tentatives, n);
      continue;
    }

    struct instance f = { 0 };
    f.which = INST_ENTRY;
    f.as.ENTRY = *i;
    if (0 == instance_cmp(&f, &q)) {
      ret = (*i)->typ;
      goto out;
    }
  }

  struct node **i = instanceset_get(&generic->finals, q);
  ret =  i == NULL ? NULL : (*i)->typ;

out:
  free(tmp.gen_args);
  unset_typ(&tmp.gen0);
  return ret;
}

char *typ_name(const struct module *mod, const struct typ *t) {
  if (typ_generic_arity(t) > 0 && !typ_is_generic_functor(t)) {
    return typ_name(mod, typ_generic_functor_const(t));
  } else if (typ_definition_const(t) != NULL) {
    return scope_name(mod, &typ_definition_const(t)->scope);
  } else {
    for (size_t n = ID_TBI__FIRST; n < ID_TBI__LAST; ++n) {
      if (mod->gctx->builtin_typs_by_name[n] == t) {
        return strdup(predefined_idents_strings[n]);
      }
    }
  }
  return NULL;
}

extern char *stpcpy(char *dest, const char *src);

static char *pptyp_defincomplete(char *r, const struct module *mod,
                                           const struct node *d) {
  char *s = r;
  s = stpcpy(s, idents_value(mod->gctx, node_ident(d)));
  s = stpcpy(s, "{");
  if (d->as.DEFINCOMPLETE.ident != ID__NONE) {
    s = stpcpy(s, "\"");
    s = stpcpy(s, idents_value(mod->gctx, d->as.DEFINCOMPLETE.ident));
    s = stpcpy(s, "\" ");
  }
  const struct node *isalist = subs_at_const(d, IDX_ISALIST);
  FOREACH_SUB_CONST(i, isalist) {
    s = stpcpy(s, "isa ");
    char *n = pptyp(mod, i->typ);
    s = stpcpy(s, n);
    free(n);
    s = stpcpy(s, " ");
  }
  FOREACH_SUB_CONST(f, d) {
    if (f->which == DEFFIELD) {
      s = stpcpy(s, idents_value(mod->gctx, node_ident(f)));
      s = stpcpy(s, ":");
      char *n = pptyp(mod, f->typ);
      s = stpcpy(s, n);
      free(n);
      s = stpcpy(s, " ");
    }
  }
  s = stpcpy(s, "}");
  return r;
}

char *pptyp(const struct module *mod, const struct typ *t) {
  if (t == NULL) {
    return strdup("(null)");
  }

  if (mod == NULL) {
    mod = typ_module_owner(t);
  }

  char *r = calloc(2048, sizeof(char));
  char *s = r;

  s = stpcpy(s, typ_is_tentative(t) ? "*" : "");

  const struct node *d = typ_definition_const(t);
  if (d == NULL) {
    s = stpcpy(s, "<ZEROED>");
  } else if (d->which == IMPORT) {
    s = stpcpy(s, "<import>");
  } else if (d->which == DEFINCOMPLETE) {
    s = pptyp_defincomplete(s, mod, d);
  } else if (typ_generic_arity(t) == 0) {
    s = stpcpy(s, typ_name(mod, t));
  } else {
    s = stpcpy(s, "(");
    if (typ_is_generic_functor(t)) {
      s = stpcpy(s, "functor ");
      s = stpcpy(s, typ_name(mod, t));
    } else {
      const struct typ *f = typ_generic_functor_const(t);
      s = stpcpy(s, typ_is_tentative(f) ? "*" : "");
      s = stpcpy(s, typ_name(mod, f));
    }

    for (size_t n = 0; n < typ_generic_arity(t); ++n) {
      const struct typ *ga = typ_generic_arg_const(t, n);
      if (ga == NULL) {
        s = stpcpy(s, " null");
      } else {
        char *s2 = pptyp(mod, ga);
        s = stpcpy(s, " ");
        s = stpcpy(s, s2);
        free(s2);
      }
    }
    s = stpcpy(s, ")");
  }

  return r;
}
