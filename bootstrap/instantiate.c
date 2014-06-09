#include "instantiate.h"

#include "types.h"
#include "topdeps.h"
#include "unify.h"

#include "passzero.h"

// Does not itself check that 'args' are valid types for instantiation.
// This is done in passfwd.c:validate_genarg_types().
static error do_instantiate(struct node **result,
                            struct module *mod,
                            const struct node *for_error, ssize_t for_error_offset,
                            struct typ *t,
                            struct typ **args, size_t arity,
                            bool tentative) {
  assert(arity == 0 || (typ_is_generic_functor(t) && arity == typ_generic_arity(t)));

  struct node *gendef = typ_definition(t);
  if (vecnode_count(&node_toplevel(gendef)->generic->instances) == 0) {
    if (for_error == NULL) {
      assert(false && "Not supposed to fail when for_error is NULL");
    } else {
      error e = mk_except_type(mod, for_error, "not a generic functor");
      EXCEPT(e);
    }
  }

  struct node *pristine = *vecnode_get(&node_toplevel(gendef)->generic->instances, 0);
  struct node *instance = add_instance_deepcopy_from_pristine(mod, gendef,
                                                              pristine, tentative);
  node_toplevel(instance)->generic->for_error = for_error;

  // Do not use set_typ()! With second-order generics, we actually do not
  // want to update this value when linking to another type during
  // unification. If the functor for instance->typ gets linked to something
  // else, then instance->typ itself must be linked to something else.
  // Making instance unreachable (through a typ).
  // See also types.c:link_generic_arg_update().
  node_toplevel(instance)->generic->our_generic_functor_typ = t;

  struct node *genargs = subs_at(instance, IDX_GENARGS);
  struct node *ga = subs_first(genargs);
  for (size_t n = 0; n < arity; ++n) {
    node_set_which(ga, SETGENARG);
    if (args[n] == NULL) {
      ga = next(ga);
      continue;
    }

    struct node *ga_arg = subs_last(ga);
    node_set_which(ga_arg, DIRECTDEF);
    set_typ(&ga_arg->as.DIRECTDEF.typ, args[n]);
    ga_arg->as.DIRECTDEF.flags = NODE_IS_TYPE;

    if (for_error != NULL) {
      ga->as.SETGENARG.for_error = for_error_offset >= 0
        ? subs_at_const(for_error, for_error_offset+n)
        : for_error;
    }

    ga = next(ga);
  }

  error e = catchup_instantiation(mod, node_module_owner(gendef),
                                  instance, tentative);
  if (e) {
    char *n = typ_pretty_name(mod, t);
    e = mk_except_type(mod, for_error, "while instantiating generic here '%s'", n);
    free(n);
    THROW(e);
  }

  *result = instance;

  return 0;
}

static struct typ *find_existing_final(struct module *mod,
                                       struct typ *t,
                                       struct typ **args,
                                       size_t arity) {
  if (arity == 0) {
    return NULL;
  }

  if (!typ_hash_ready(t)) {
    return NULL;
  }

  const struct node *d = typ_definition_const(t);
  const struct toplevel *toplevel = node_toplevel_const(d);
  for (size_t n = 1, count = vecnode_count(&toplevel->generic->instances); n < count; ++n) {
    struct typ *i = (*vecnode_get(&toplevel->generic->instances, n))->typ;

    if (typ_definition(i) == NULL || typ_is_tentative(i)) {
      continue;
    }

    size_t a;
    for (a = 0; a < arity; ++a) {
      if (!typ_equal(typ_generic_arg_const(i, a), args[a])) {
        break;
      }
    }

    if (a == arity) {
      return i;
    }
  }

  return NULL;
}

// Same as find_existing_final(), but with different arguments.
struct typ *find_existing_final_for_tentative(struct module *mod,
                                              const struct typ *t) {
  if (!typ_hash_ready(t)) {
    return NULL;
  }

  const struct node *d = typ_definition_const(typ_generic_functor_const(t));

  const struct toplevel *toplevel = node_toplevel_const(d);
  for (size_t n = 1, count = vecnode_count(&toplevel->generic->instances); n < count; ++n) {
    struct typ *i = (*vecnode_get(&toplevel->generic->instances, n))->typ;
    if (!typ_is_tentative(i) && typ_equal(t, i)) {
      return i;
    }
  }

  return NULL;
}

// Allows self-referential instances, such as, in (slice t):
//  alias us = slice t
static struct typ *find_existing_identical(struct module *mod,
                                           struct typ *t,
                                           struct typ **args,
                                           size_t arity) {
  if (arity == 0) {
    return NULL;
  }

  const struct node *d = typ_definition_const(t);
  const struct toplevel *toplevel = node_toplevel_const(d);
  for (size_t n = 1, count = vecnode_count(&toplevel->generic->instances); n < count; ++n) {
    struct typ *i = (*vecnode_get(&toplevel->generic->instances, n))->typ;

    // Use pointer comparison: we're looking for the exact same arguments
    // (same tentative bit, etc.).

    if (typ_definition(i) == NULL || typ_generic_functor_const(i) != t) {
      continue;
    }

    size_t a;
    for (a = 0; a < arity; ++a) {
      if (typ_generic_arg_const(i, a) != args[a]) {
        break;
      }
    }

    if (a == arity) {
      return i;
    }
  }

  return NULL;
}

struct typ *tentative_generic_arg(struct module *mod, const struct node *for_error,
                                  struct typ *t, size_t n) {
  struct typ *ga = typ_generic_arg(t, n);
  const size_t arity = typ_generic_arity(ga);

  if (arity == 0) {
    assert(!typ_is_tentative(ga));
    return instantiate_fully_implicit(mod, for_error, ga)->typ;
  } else if (typ_is_generic_functor(ga)) {
    return typ_create_tentative_functor(ga);
  }

  // Otherwise, the generic argument is declared as c:(`container t), where
  // t is another generic argument. The typ will be created by the type
  // inference step.
  return NULL;
}

static bool instantiation_is_tentative(const struct module *mod,
                                       struct typ *t, struct typ **args,
                                       size_t arity) {
  if (mod->state->tentatively || mod->state->top_state != NULL) {
    if (arity == 0) {
      return true;
    }

    if (typ_is_tentative(t)) {
      return true;
    }

    for (size_t n = 0; n < arity; ++n) {
      if (args[n] == NULL) {
        return true;
      }
      if (typ_is_tentative(args[n])) {
        return true;
      }
    }

    // Optimisation: immediately turn into a final instantiation.
    return false;
  } else {
    return false;
  }
}

error instantiate(struct node **result,
                  struct module *mod,
                  const struct node *for_error, size_t for_error_offset,
                  struct typ *t, struct typ **args, size_t arity) {
  assert(arity == typ_generic_arity(t));

  struct typ *r = find_existing_identical(mod, t, args, arity);
  if (r != NULL) {
    if (result != NULL) {
      *result = typ_definition(r);
    }
    return 0;
  }

  const bool tentative = instantiation_is_tentative(mod, t, args, arity);

  if (!tentative) {
    struct typ *r = find_existing_final(mod, t, args, arity);
    if (r != NULL) {
      if (result != NULL) {
        *result = typ_definition(r);
      }
      return 0;
    }
  }

  error e = do_instantiate(result, mod, for_error, for_error_offset,
                           t, args, arity, tentative);
  EXCEPT(e);

  return 0;
}

struct node *instantiate_fully_implicit(struct module *mod,
                                        const struct node *for_error,
                                        struct typ *t) {
  assert(typ_is_generic_functor(t) || typ_generic_arity(t) == 0);

  const size_t gen_arity = typ_generic_arity(t);
  struct typ **args = calloc(gen_arity, sizeof(struct typ *));
  for (size_t n = 0; n < gen_arity; ++n) {
    assert(!typ_is_tentative(typ_generic_arg(t, n)));
    args[n] = tentative_generic_arg(mod, for_error, t, n);
    assert(args[n] == NULL || typ_is_tentative(args[n]));
  }

  struct node *i = NULL;
  error e = instantiate(&i, mod, for_error, -1,
                        t, args, gen_arity);
  assert(!e);

  if (gen_arity == 0) {
    assert(typ_is_tentative(i->typ));
  } else {
    assert(typ_is_tentative(typ_generic_functor(i->typ)) == typ_is_tentative(t));
  }
  for (size_t n = 0; n < gen_arity; ++n) {
    assert(typ_is_tentative(typ_generic_arg(i->typ, n)));
  }

  free(args);

  return i;
}