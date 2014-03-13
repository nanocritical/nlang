#include "passzero.h"

#include "scope.h"

static STEP_FILTER(step_do_rewrite_prototype_wildcards,
                   SF(UN));
static error step_do_rewrite_prototype_wildcards(struct module *mod, struct node *node,
                                                 void *user, bool *stop) {
  DSTEP(mod, node);
  if (node->as.UN.operator == TREFWILDCARD
      || node->as.UN.operator == TNULREFWILDCARD) {
    // FIXME The proper solution is to use
    //   (intf t:Any) `nullable r:(`ref t) = (`any_ref t)
    // instead of `nullable_ref, `nullable_mutable_ref, and `nullable_mercurial_ref.
    // and use (`nullable __wildcard_ref_arg__) here.
    assert(node->as.UN.operator != TNULREFWILDCARD && "FIXME: Unsupported");

    node_set_which(node, CALL);
    struct node *d = mk_node(mod, node, IDENT);
    d->as.IDENT.name = ID_WILDCARD_REF_ARG;
    node_subs_remove(node, d);
    node_subs_insert_before(node, node_subs_first(node), d);
  }
  return 0;
}

static error pass_rewrite_wildcards(struct module *mod, struct node *root,
                                    void *user, ssize_t shallow_last_up) {
  PASS(, UP_STEP(step_do_rewrite_prototype_wildcards));
  return 0;
}

static STEP_FILTER(step_rewrite_prototype_wildcards,
                   SF(DEFFUN) | SF(DEFMETHOD));
static error step_rewrite_prototype_wildcards(struct module *mod, struct node *node,
                                              void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *funargs = node_subs_at(node, IDX_FUNARGS);
  FOREACH_SUB(arg, funargs) {
    PUSH_STATE(mod->state->step_state);
    error e = pass_rewrite_wildcards(mod, arg, NULL, -1);
    EXCEPT(e);
    POP_STATE(mod->state->step_state);
  }

  return 0;
}

static void add_generic(struct node *node) {
  struct toplevel *toplevel = node_toplevel(node);
  if (toplevel->generic == NULL) {
    toplevel->generic = calloc(1, sizeof(*toplevel->generic));
  }
}

struct node *add_instance_deepcopy_from_pristine(struct module *mod,
                                                 struct node *node,
                                                 struct node *pristine,
                                                 bool tentative) {
  struct node *instance = calloc(1, sizeof(struct node));
  node_deepcopy(mod, instance, pristine);
  node_toplevel(instance)->scope_name = 0;

  add_generic(node);
  add_generic(instance);

  if (!tentative) {
    struct generic *generic = node_toplevel(node)->generic;
    const size_t idx = generic->instances_count;
    generic->instances_count += 1;
    generic->instances = realloc(generic->instances,
                                 generic->instances_count * sizeof(*generic->instances));
    generic->instances[idx] = instance;
  }

  return instance;
}

static STEP_FILTER(step_generics_pristine_copy,
                   SF(DEFTYPE) | SF(DEFINTF) | SF(DEFFUN) | SF(DEFMETHOD));
static error step_generics_pristine_copy(struct module *mod, struct node *node,
                                         void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *genargs = node_subs_at(node, IDX_GENARGS);
  switch (node->which) {
  case DEFTYPE:
  case DEFINTF:
    if (node_subs_count_atleast(genargs, 1)
        && node_subs_first(genargs)->which == DEFGENARG) {
      (void) add_instance_deepcopy_from_pristine(mod, node, node, false);
    }
    break;
  case DEFFUN:
  case DEFMETHOD:
    // Always needed because the method/fun could be part of a generic
    // DEFTYPE, and we cannot know that yet. If the parent is not a generic,
    // we will remove this unneeded stuff in do_move_detached_member().
    (void) add_instance_deepcopy_from_pristine(mod, node, node, false);
    break;
  default:
    assert(false && "Unreached");
    break;
  }

  return 0;
}

static STEP_FILTER(step_detect_prototypes,
                   SF(DEFFUN) | SF(DEFMETHOD));
static error step_detect_prototypes(struct module *mod, struct node *node,
                                    void *user, bool *stop) {
  DSTEP(mod, node);

  struct toplevel *toplevel = node_toplevel(node);
  if (toplevel->builtingen == BG__NOT && !node_has_tail_block(node)) {
    toplevel->flags |= TOP_IS_PROTOTYPE;
  }

  return 0;
}

static error do_check_deftype_kind(struct module *mod, struct node *deft,
                                   struct node *node) {
  error e;
  enum deftype_kind k = deft->as.DEFTYPE.kind;
  FOREACH_SUB(f, node) {
    switch (f->which) {
    case DEFFIELD:
      if (k == DEFTYPE_ENUM) {
        e = mk_except_type(mod, f, "enum may not contain a field declaration");
        THROW(e);
      }
      break;
    case DEFCHOICE:
      if (k == DEFTYPE_STRUCT) {
        e = mk_except_type(mod, f, "struct may not contain a choice declaration");
        THROW(e);
      }

      e = do_check_deftype_kind(mod, deft, f);
      EXCEPT(e);
      break;
    default:
      break;
    }
  }
  return 0;
}

static STEP_FILTER(step_check_deftype_kind,
                   SF(DEFTYPE));
// Must be run before builtins are added.
static error step_check_deftype_kind(struct module *mod, struct node *node,
                                     void *user, bool *stop) {
  DSTEP(mod, node);
  error e = do_check_deftype_kind(mod, node, node);
  EXCEPT(e);
  return 0;
}

static void do_assign_defchoice_tag(struct module *mod,
                                    struct node *parent, struct node *prev,
                                    struct node *node) {
  node->as.DEFCHOICE.is_leaf = true;
  if (node->as.DEFCHOICE.has_tag) {
    return;
  }

  struct node *tag;
  if (prev == NULL && parent->which != DEFCHOICE) {
    tag = mk_node(mod, node, NUMBER);
    tag->as.NUMBER.value = "0";
  } else {
    if (prev != NULL) {
      struct node *pred = node_subs_at(prev, IDX_CH_TAG_FIRST);
      tag = mk_node(mod, node, BIN);
      tag->as.BIN.operator = TPLUS;
      struct node *left = node_new_subnode(mod, tag);
      node_deepcopy(mod, left, pred);
      struct node *one = mk_node(mod, tag, NUMBER);
      one->as.NUMBER.value = "1";
    } else if (parent->which == DEFCHOICE) {
      tag = node_new_subnode(mod, node);
      node_deepcopy(mod, tag, node_subs_at(parent, IDX_CH_TAG_FIRST));
    } else {
      return;
    }

  }

  node_subs_remove(node, tag);
  node_subs_insert_after(node, node_subs_at(node, IDX_CH_TAG_FIRST-1), tag);

  node->as.DEFCHOICE.has_tag = true;
  if (parent->which == DEFCHOICE) {
    parent->as.DEFCHOICE.is_leaf = false;
  }
}

static STEP_FILTER(step_assign_defchoice_tag_down,
                   SF(DEFTYPE) | SF(DEFCHOICE));
static error step_assign_defchoice_tag_down(struct module *mod, struct node *node,
                                            void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *prev = NULL;
  FOREACH_SUB(d, node) {
    if (d->which == DEFCHOICE) {
      do_assign_defchoice_tag(mod, node, prev, d);
      prev = d;
    }
  }

  return 0;
}

static STEP_FILTER(step_assign_defchoice_tag_up,
                   SF(DEFCHOICE));
static error step_assign_defchoice_tag_up(struct module *mod, struct node *node,
                                          void *user, bool *stop) {
  DSTEP(mod, node);
  if (node->as.DEFCHOICE.is_leaf) {
    struct node *dummy = mk_node(mod, node, NUMBER);
    dummy->as.NUMBER.value = "0";
    node_subs_remove(node, dummy);
    node_subs_insert_after(node, node_subs_at(node, IDX_CH_TAG_FIRST), dummy);
    return 0;
  }

  REVERSE_FOREACH_SUB(last, node) {
    if (last->which == DEFCHOICE) {
      struct node *last_tag = node_new_subnode(mod, node);
      node_deepcopy(mod, last_tag, node_subs_at(last, IDX_CH_TAG_FIRST));
      node_subs_remove(node, last_tag);
      node_subs_insert_after(node, node_subs_at(node, IDX_CH_TAG_FIRST), last_tag);
      break;
    }
  }

  return 0;
}

STEP_FILTER(step_add_scopes,
            -1);
error step_add_scopes(struct module *mod, struct node *node,
                      void *user, bool *stop) {
  DSTEP(mod, node);

  FOREACH_SUB(s, node) {
    s->scope.parent = &node->scope;
  }

  return 0;
}

STEP_FILTER(step_stop_submodules,
            SF(MODULE));
error step_stop_submodules(struct module *mod, struct node *node,
                           void *user, bool *stop) {
  DSTEP(mod, node);

  int *module_depth = user;
  *module_depth += 1;

  if (*module_depth > 1) {
    *stop = true;
  }
  return 0;
}

static error passzero0(struct module *mod, struct node *root,
                       void *user, ssize_t shallow_last_up) {
  PASS(
    DOWN_STEP(step_rewrite_prototype_wildcards);
    DOWN_STEP(step_generics_pristine_copy);
    DOWN_STEP(step_detect_prototypes);
    DOWN_STEP(step_check_deftype_kind);
    DOWN_STEP(step_assign_defchoice_tag_down);
    ,
    UP_STEP(step_assign_defchoice_tag_up);
    UP_STEP(step_add_scopes);
    );
  return 0;
}

a_pass passzero[] = { passzero0 };
