#include "constraints.h"

#include "passes.h"
#include "types.h"
#include "parser.h"
#include "phi.h"
#include "printer.h"
#include <stdarg.h>

typedef int8_t cbool;

enum cbool_values {
  U = 0,
  N = -1,
  Y = 1,
};

enum constraint_builtins {
  CBI__ANYTAG,
  CBI_INIT,
  CBI_NONNULL,
  CBI_EQTRUE,
  CBI_VALID,
  CBI__NUM,
};

enum constraint2_builtins {
  CBI2_EQ,
  CBI2__NUM,
};

static const char *constraint_builtins_strings[CBI__NUM] = {
  [CBI_INIT] = "INIT",
  [CBI_NONNULL] = "NONNULL",
  [CBI_EQTRUE] = "EQTRUE",
  [CBI_VALID] = "VALID",
};

static const char *constraint2_builtins_strings[CBI__NUM] = {
  [CBI2_EQ] = "EQ",
};

HTABLE_SPARSE(tagset, cbool, ident);
IMPLEMENT_HTABLE_SPARSE(unused__ static, tagset, cbool, ident,
                        ident_hash, ident_cmp);

// An boolean_hypothesis is additional constraints that result from a single
// boolean condition being assumed to be true or false.
struct hypothesis {
  struct constraint *if_true;
  struct constraint *if_false;
};

//struct tag_alsoif {
//};
//
//struct value_alsoif {
//};
//
//union alsoif_as {
//  struct boolean_alsoif HYP_BOOLEAN;
//  struct tag_alsoif HYP_TAG;
//  struct value_alsoif HYP_VALUE;
//};
//
//enum alsoif_which {
//  HYP_BOOLEAN,
//  HYP_TAG,
//  HYP_VALUE,
//};
//
//struct alsoif {
//  enum alsoif_which which;
//  union alsoif_as as;
//};

HTABLE_SPARSE(hypmap, struct hypothesis, struct node *);
IMPLEMENT_HTABLE_SPARSE(unused__ static, hypmap, struct hypothesis,
                        struct node *, node_ptr_hash, node_ptr_cmp);

struct relation {
  enum constraint2_builtins cbi2;
  struct node *node;
  cbool value;
};

VECTOR(vecrel, struct relation, 1);
IMPLEMENT_VECTOR(unused__ static, vecrel, struct relation);

static inline use_result__ ssize_t vecrel_remove_replace_with_last(struct vecrel *v, ssize_t n) {
  const size_t count = vecrel_count(v);
  if (count == 1) {
    vecrel_destroy(v);
    return 0;
  }

  struct relation last = vecrel_pop(v);
  if (n + 1 == count) {
    (void) last;
    return 0;
  } else {
    struct relation *loc = vecrel_get(v, n);
    *loc = last;
    return -1;
  }
}

struct constraint {
  cbool table[CBI__NUM];
  struct vecrel rels[CBI2__NUM];

  struct tagset tags;
  // FIXME: 'tags' is not normalized: it can be full of 'U' that while
  // considered to be deleted by the underlying table, do appear in
  // tagset_count().
  // FIXME: a set is not a nice structure, as variants are hierarchical, and
  // 'tags' should reflect that.
  // FIXME: need to guarantee that all tags are set iff CBI__ANYTAG==Y

  struct hypmap under;

  struct vecnode *users; // only for DEFNAME and DEFARG constraints
};

static struct node *block_value_statement(struct node *node) {
  assert(node->which == BLOCK);
  struct node *s = subs_last(node);
  while (typ_equal(s->typ, TBI__NOT_TYPEABLE)) {
    s = prev(s);
  }
  return s;
}

static struct node *ident_expr_def(struct node *node) {
  switch (node->which) {
  case DEFNAME:
    return node;
  case IDENT:
    return node->as.IDENT.def;
  case BLOCK:
    return ident_expr_def(block_value_statement(node));
  case PHI:
    return node->as.PHI.def;
  default:
    return NULL;
  }
}

static void add_user(struct node *node, struct node *user) {
  struct node *def = ident_expr_def(node);
  if (def == NULL) {
    return;
  }

  struct constraint *dc = def->constraint;
  for (size_t n = 0, count = vecnode_count(dc->users); n < count; ++n) {
    struct node *u = *vecnode_get(dc->users, n);
    if (u == user) {
      return;
    }
  }

  vecnode_push(dc->users, node);
}

void constraint_invariant(const struct constraint *c) {
  for (size_t cbi = 0; cbi < CBI__NUM; ++cbi) {
    assert(c->table[cbi] >= N && c->table[cbi] <= Y);
  }
}

static struct constraint *new_constraint(struct module *mod) {
  STATIT {
    statit_mempool.count_by_type[1] += 1;
    statit_mempool.size_by_type[1] += sizeof(struct constraint);
  }
  struct constraint *c = mempool_calloc(mod, 1, sizeof(struct constraint));

  tagset_init(&c->tags, 0);
  tagset_set_delete_val(&c->tags, U);

  hypmap_init(&c->under, 0);

  return c;
}

static struct constraint *do_assuming(struct module *mod, struct node *node,
                                      struct node *cond, bool reversed,
                                      bool create) {
  struct constraint *c = node->constraint;
  cond = ident_expr_def(cond);
  assert(NM(cond->which) & (NM(DEFNAME) | NM(DEFARG)));

  struct hypothesis *existing = hypmap_get(&c->under, cond);
  if (existing == NULL) {
    if (!create) {
      return NULL;
    }

    struct hypothesis hyp = { 0 };
    if (reversed) {
      hyp.if_false = new_constraint(mod);
      add_user(cond, node);
    } else {
      hyp.if_true = new_constraint(mod);
      add_user(cond, node);
    }
    hypmap_set(&c->under, cond, hyp);

    return reversed ? hyp.if_false : hyp.if_true;

  } else {
    if (reversed) {
      if (create && existing->if_false == NULL) {
        existing->if_false = new_constraint(mod);
      }
      return existing->if_false;
    } else {
      if (create && existing->if_true == NULL) {
        existing->if_true = new_constraint(mod);
      }
      return existing->if_true;
    }
  }
}

static struct constraint *assuming(struct module *mod, struct node *node,
                                   struct node *cond, bool reversed) {
  return do_assuming(mod, node, cond, reversed, true);
}

static struct constraint *try_assuming(struct module *mod, struct node *node,
                                       struct node *cond, bool reversed) {
  return do_assuming(mod, node, cond, reversed, false);
}

static int merge_hypothesis_each(const struct node **cond,
                                 struct hypothesis *hyp,
                                 void *user) {
  struct hypmap *c_under = user;
  hypmap_set(c_under, *cond, *hyp);
  return 0;
}

static void constraint_merge_hypothesis(struct constraint *c,
                                        struct constraint *a) {
  for (size_t cbi = 0; cbi < CBI__NUM; ++cbi) {
    if (a->table[cbi] != U) {
      c->table[cbi] = a->table[cbi];
    }
  }

  if (a->table[CBI__ANYTAG] == N) {
    tagset_copy(&c->tags, &a->tags);
  }

  if (hypmap_count(&a->under) != 0) {
    hypmap_foreach(&a->under, merge_hypothesis_each, &c->under);
  }
}

static void constraint_set(struct module *mod, struct constraint *c,
                           enum constraint_builtins cbi, bool reversed) {
  INVARIANT_CONSTRAINT(c);
  c->table[cbi] = reversed ? N : Y;
}

static void constraint_unset(struct module *mod, struct constraint *c,
                             enum constraint_builtins cbi) {
  INVARIANT_CONSTRAINT(c);
  c->table[cbi] = U;
}

static void constraint_set_rel(struct module *mod, struct node *node,
                               enum constraint2_builtins cbi2,
                               struct node *other, bool reversed) {
  struct constraint *c = node->constraint;
  struct vecrel *rels = &c->rels[cbi2];
  for (size_t n = 0, count = vecrel_count(rels); n < count; ++n) {
    struct relation *r = vecrel_get(rels, n);
    if (r->cbi2 != cbi2 || r->node != other) {
      continue;
    }

    r->value = reversed ? N : Y;
    return;
  }

  struct relation r = {
    .cbi2 = cbi2,
    .node = other,
    .value = reversed ? N : Y,
  };
  vecrel_push(rels, r);
  add_user(other, node);
}

static void constraint_unset_rel(struct module *mod, struct constraint *c,
                                 enum constraint2_builtins cbi2,
                                 struct node *other) {
  struct vecrel *rels = &c->rels[cbi2];
  for (ssize_t n = 0, count = vecrel_count(rels); n < count; ++n) {
    const struct relation *r = vecrel_get(rels, n);
    if (r->cbi2 != cbi2 || r->node != other) {
      continue;
    }

    n += vecrel_remove_replace_with_last(rels, n);
    // Not removing user.
    return;
  }
}

static cbool constraint_get_rel(struct module *mod, struct constraint *c,
                                enum constraint2_builtins cbi2,
                                struct node *node) {
  struct vecrel *rels = &c->rels[cbi2];
  for (ssize_t n = 0, count = vecrel_count(rels); n < count; ++n) {
    const struct relation *r = vecrel_get(rels, n);
    if (r->cbi2 != cbi2 || r->node != node) {
      continue;
    }

    return r->value;
  }

  return U;
}

static cbool constraint_get_tag(struct constraint *c, ident tag) {
  if (c->table[CBI__ANYTAG] == Y || c->table[CBI__ANYTAG] == U) {
    return c->table[CBI__ANYTAG];
  }

  const cbool *existing = tagset_get(&c->tags, tag);
  if (existing == NULL || *existing != Y) {
    return N;
  } else {
    return Y;
  }
}

static void constraint_set_tag(struct module *mod, struct constraint *c,
                               ident tag, bool reversed) {
  INVARIANT_CONSTRAINT(c);
  c->table[CBI__ANYTAG] = N;
  tagset_set(&c->tags, tag, reversed ? N : Y);
}

static void constraint_unset_tag(struct module *mod, struct constraint *c,
                                 ident tag) {
  INVARIANT_CONSTRAINT(c);
  tagset_set(&c->tags, tag, U);
  // FIXME: unable to reset CBI__ANYTAG to Y because we can't trust
  // tagset_count() with many U's.
}

static int equal_tag_each(const ident *name, cbool *value, void *user) {
  struct tagset *b = user;
  const cbool *other = tagset_get(b, *name);
  return other == NULL || *value != *other;
}

static bool constraint_equal(struct constraint *a, struct constraint *b);

static int equal_hypothesis_each(const struct node **cond,
                                 struct hypothesis *hyp,
                                 void *user) {
  struct hypmap *b = user;
  const struct hypothesis *other = hypmap_get(b, *cond);
  return other == NULL
    || !constraint_equal(hyp->if_true, other->if_true)
    || !constraint_equal(hyp->if_false, other->if_false);
}

static bool constraint_equal(struct constraint *a, struct constraint *b) {
  if (a == b) {
    return true;
  }

  if (a == NULL || b == NULL) {
    return false;
  }

  int c = memcmp(a->table, b->table, sizeof(a->table));
  if (c != 0) {
    return false;
  }

  if (a->table[CBI__ANYTAG] == N && b->table[CBI__ANYTAG] == N) {
    if (tagset_count(&a->tags) != tagset_count(&b->tags)) {
      return false;
    }

    if (tagset_foreach(&a->tags, equal_tag_each, &b->tags)) {
      return false;
    }
  }

  if (hypmap_count(&a->under) != hypmap_count(&b->under)) {
    return false;
  }

  if (hypmap_foreach(&a->under, equal_hypothesis_each, &b->under)) {
    return false;
  }

  return true;
}

struct snprint_constraint_state {
  const struct module *mod;
  char *s;
  size_t len;
  size_t pos;
  bool do_nos;
  bool has_yess;
  bool has_nos;
  size_t hyp_depth;
  size_t hyp_printed;
};

static int snprint_constraint_tag_each(const ident *tag, cbool *value,
                                       void *user) {
  struct snprint_constraint_state *st = user;
  if ((*value == Y && !st->do_nos)
      || (*value == N && st->do_nos)) {
    int n = snprintf(st->s, st->len,
                     "|%s", idents_value(st->mod->gctx, *tag));
    st->s += n;
    st->len -= n;
    st->has_yess = true;
  } else if (*value == N) {
    st->has_nos = true;
  }
  return 0;
}

static int do_snprint_constraint(char *s, size_t len,
                                 const struct module *mod,
                                 const struct constraint *_c,
                                 size_t hyp_depth);

static int snprindent(char *s, size_t len, size_t hyp_depth) {
  size_t pos = snprintf(s, len, "\n");
  for (size_t n = 0; n < hyp_depth + 1; ++n) {
    pos += snprintf(s+pos, len-pos, "\t");
  }
  return pos;
}

static int snprint_constraint_under_each(const struct node **_cond,
                                         struct hypothesis *hyp,
                                         void *user) {
  struct node *cond = CONST_CAST(*_cond);
  struct snprint_constraint_state *st = user;

  if (hyp->if_true != NULL) {
    int n = 0;
    if (st->hyp_printed > 0) {
      n = snprindent(st->s, st->len, st->hyp_depth);
      st->s += n;
      st->len -= n;
      n = snprintf(st->s, st->len, "and ");
      st->s += n;
      st->len -= n;
    }

    n = snprintf(st->s, st->len, "(%s => ",
                 scope_name(st->mod, cond));
    st->s += n;
    st->len -= n;

    n = do_snprint_constraint(st->s, st->len, st->mod, hyp->if_true, st->hyp_depth+1);
    st->s += n;
    st->len -= n;

    n = snprintf(st->s, st->len, ")");
    st->s += n;
    st->len -= n;

    st->hyp_printed += 1;
  }

  if (hyp->if_false != NULL) {
    int n = 0;
    if (st->hyp_printed > 0) {
      n = snprindent(st->s, st->len, st->hyp_depth);
      st->s += n;
      st->len -= n;
      n = snprintf(st->s, st->len, "and ");
      st->s += n;
      st->len -= n;
    }

    n = snprintf(st->s, st->len, "(not %s => ",
                 scope_name(st->mod, cond));
    st->s += n;
    st->len -= n;

    n = do_snprint_constraint(st->s, st->len, st->mod, hyp->if_false, st->hyp_depth+1);
    st->s += n;
    st->len -= n;

    n = snprintf(st->s, st->len, ")");
    st->s += n;
    st->len -= n;

    st->hyp_printed += 1;
  }

  return 0;
}

static int snprint_constraint_rel(char *s, size_t len, const struct module *mod,
                                  const struct relation *rel) {
  size_t pos = 0;
  pos += snprintf(s+pos, len-pos, "%s %s",
                  constraint2_builtins_strings[rel->cbi2],
                  scope_name(mod, rel->node));
  return pos;
}

static int do_snprint_constraint(char *s, size_t len,
                                 const struct module *mod,
                                 const struct constraint *_c,
                                 size_t hyp_depth) {
  struct constraint *c = CONST_CAST(_c);
  size_t pos = 0;
  pos += snprintf(s+pos, len-pos, "(");

  bool first = true;
  for (size_t cbi = CBI__ANYTAG+1; cbi < CBI__NUM; ++cbi) {
    const cbool v = c->table[cbi];
    if (v != U && !first) {
      pos += snprindent(s+pos, len-pos, hyp_depth);
      pos += snprintf(s+pos, len-pos, "and ");
    }

    if (v == Y) {
      first = false;
      pos += snprintf(s+pos, len-pos, "%s", constraint_builtins_strings[cbi]);
    } else if (v == N) {
      first = false;
      pos += snprintf(s+pos, len-pos, "not %s", constraint_builtins_strings[cbi]);
    }
  }

  for (size_t cbi2 = 0; cbi2 < CBI2__NUM; ++cbi2) {
    struct vecrel *rels = &c->rels[cbi2];
    if (vecrel_count(rels) == 0) {
      continue;
    }

    for (size_t n = 0, count = vecrel_count(rels); n < count; ++n) {
      if (!first) {
        pos += snprindent(s+pos, len-pos, hyp_depth);
        pos += snprintf(s+pos, len-pos, "and ");
      }

      const size_t printed = snprint_constraint_rel(s+pos, len-pos, mod,
                                                    vecrel_get(rels, n));
      first = first && printed == 0;
      pos += printed;
    }
  }

  if (c->table[CBI__ANYTAG] == N) {
    if (!first) {
      pos += snprindent(s+pos, len-pos, hyp_depth);
      pos += snprintf(s+pos, len-pos, "and ");
    }

    struct snprint_constraint_state st = {
      .mod = mod,
      .s = s + pos,
      .len = len - pos,
      .do_nos = false,
      .has_yess = false,
      .has_nos = false,
      .hyp_depth = hyp_depth,
      .hyp_printed = 0,
    };

    tagset_foreach(&c->tags, snprint_constraint_tag_each, &st);

    first &= !(st.has_yess || st.has_nos);

    if (st.has_nos) {
      if (st.has_yess) {
        int n0 = snprindent(st.s, st.len, hyp_depth);
        st.s += n0;
        st.len -= n0;
        int n = snprintf(st.s, st.len, "and not ");
        st.s += n;
        st.len -= n;
      }

      st.do_nos = true;
      tagset_foreach(&c->tags, snprint_constraint_tag_each, &st);
    }

    pos += len - pos - st.len;
  }

  if (hypmap_count(&c->under) != 0) {
    if (!first) {
      pos += snprindent(s+pos, len-pos, hyp_depth);
      pos += snprintf(s+pos, len-pos, "and ");
    }

    struct snprint_constraint_state st = {
      .mod = mod,
      .s = s + pos,
      .len = len - pos,
      0
    };

    hypmap_foreach(&c->under, snprint_constraint_under_each, &st);

    pos += len - pos - st.len;
  }

  pos += snprintf(s+pos, len-pos, ")");
  return pos;
}

int snprint_constraint(char *s, size_t len,
                       const struct module *mod,
                       const struct constraint *_c) {
  return do_snprint_constraint(s, len, mod, _c, 0);
}

unused__ static void ppconstraint(struct module *mod, struct constraint *c) {
  char s[1024] = { 0 };
  size_t len = ARRAY_SIZE(s);
  snprint_constraint(s, len, mod, c);
  fprintf(stderr, "%s\n", s);
}

EXAMPLE_NCC_EMPTY(snprint_constraint) {
  char s[512] = { 0 };
  size_t len = ARRAY_SIZE(s);

  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set(mod, n->constraint, CBI_INIT, false);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strcmp(s, "(INIT)") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set(mod, n->constraint, CBI_INIT, true);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strcmp(s, "(not INIT)") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set(mod, n->constraint, CBI_INIT, true);
    constraint_unset(mod, n->constraint, CBI_INIT);
    assert(2 == snprint_constraint(s, len, mod, n->constraint));
    assert(strcmp(s, "()") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set(mod, n->constraint, CBI_NONNULL, false);
    constraint_unset(mod, n->constraint, CBI_NONNULL);
    constraint_set(mod, n->constraint, CBI_NONNULL, true);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strcmp(s, "(not NONNULL)") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set(mod, n->constraint, CBI_INIT, false);
    constraint_set(mod, n->constraint, CBI_NONNULL, true);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strcmp(s, "(INIT\n\tand not NONNULL)") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set_tag(mod, n->constraint, ID_C, false);
    constraint_unset_tag(mod, n->constraint, ID_C);
    constraint_set_tag(mod, n->constraint, ID_C, false);
    assert(4 == snprint_constraint(s, len, mod, n->constraint));
    assert(strcmp(s, "(|c)") == 0);
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set_tag(mod, n->constraint, ID_C, false);
    constraint_set_tag(mod, n->constraint, ID_OTHER, false);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strlen(s) == strlen("(|c|other)"));
  }
  {
    struct node *n = mk_node(mod, mod->body, IDENT);
    n->constraint = new_constraint(mod);
    constraint_set_tag(mod, n->constraint, ID_C, false);
    constraint_set_tag(mod, n->constraint, ID_OTHER, true);
    snprint_constraint(s, len, mod, n->constraint);
    assert(strcmp(s, "(|c\n\tand not |other)") == 0);
  }
  {
    struct node *let = mk_node(mod, mod->body, LET);
    struct node *d = mk_node(mod, let, DEFNAME);
    d->constraint = new_constraint(mod);
    d->constraint->users = calloc(1, sizeof(*d->constraint->users));
    struct node *name = mk_node(mod, d, IDENT);
    name->as.IDENT.name = ID_NEXT;

    struct node *na = node_new_subnode(mod, mod->body);
    na->constraint = new_constraint(mod);
    na->constraint->table[CBI_INIT] = Y;
    constraint_set_tag(mod, na->constraint, ID_C, true);

    struct constraint *c = assuming(mod, na, d, false);
    c->table[CBI_NONNULL] = Y;
    c->table[CBI_VALID] = N;

    snprint_constraint(s, len, mod, na->constraint);
    assert(strcmp(s, "(INIT\n\tand |c\n\tand (bootstrap.mockempty.<let>.Next"
                  " => (NONNULL\n\t\tand not VALID)))") == 0);
  }
}

static ERROR mk_except_constraint(const struct module *mod,
                                  const struct node *node,
                                  const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char s[2048] = { 0 };
  size_t pos = 0, len = ARRAY_SIZE(s);

  pos += snprint_codeloc(s+pos, len-pos, mod, node);
  pos += snprintf(s+pos, len-pos, "constraint: ");
  pos += vsnprintf(s+pos, len-pos, fmt, ap);
  if (node != NULL) {
    pos += snprintf(s+pos, len-pos, ":\n");
    pos += snprint_constraint(s+pos, len-pos, mod, node->constraint);
  }

  error e = 0;
  GOTO_THROWF(EINVAL, "%s", s);

except:
  va_end(ap);
  return e;
}

static int constraint_get_single_tag_each(const ident *tag, cbool *value,
                                          void *user) {
  if (*value != U) {
    ident *result = user;
    if (*result != ID__NONE) {
      return 1;
    }
    *result = *tag;
  }
  return 0;
}

static ERROR constraint_get_single_tag(ident *tag,
                                       const struct module *mod,
                                       const struct node *node) {
  error e;
  struct constraint *c = node->constraint;
  INVARIANT_CONSTRAINT(c);

  if (c->table[CBI__ANYTAG] != N) {
    goto many;
  }

  *tag = ID__NONE;
  int too_many = tagset_foreach(&c->tags, constraint_get_single_tag_each, tag);
  if (too_many) {
    goto many;
  } else if (*tag == ID__NONE) {
    e = mk_except_constraint(mod, node, "no known tag");
    THROW(e);
  }

  return 0;

many:
  e = mk_except_constraint(mod, node, "too many possible tags");
  THROW(e);
}

static void constraint_copy(struct module *mod,
                            struct constraint *cdst,
                            const struct constraint *csrc);

static int deepcopy_hypothesis_each(const struct node **cond,
                                    struct hypothesis *hyp, void *user) {
  struct module *mod = user;

  struct constraint *if_true = hyp->if_true;
  if (if_true != NULL) {
    hyp->if_true = new_constraint(mod);
    constraint_copy(mod, hyp->if_true, if_true);
  }

  struct constraint *if_false = hyp->if_false;
  if (if_false != NULL) {
    hyp->if_false = new_constraint(mod);
    constraint_copy(mod, hyp->if_false, if_false);
  }

  return 0;
}

static void constraint_copy(struct module *mod,
                            struct constraint *cdst,
                            const struct constraint *csrc) {
  memcpy(cdst->table, csrc->table, sizeof(cdst->table));
  tagset_copy(&cdst->tags, &csrc->tags);
  hypmap_copy(&cdst->under, &csrc->under);
  hypmap_foreach(&cdst->under, deepcopy_hypothesis_each, mod);
}

struct merge_state {
  struct module *mod;
  struct constraint *target;
  const struct node *for_error;
};

static ERROR constraint_check(struct module *mod, const struct node *node,
                              enum constraint_builtins cbi, bool reversed) {
  struct constraint *c = node->constraint;
  if (c->table[cbi] != Y) {
    const char *not = reversed ? "not " : "";
    error e = mk_except_constraint(mod, node, "'%s%s' constraint not satisfied",
                                   not, constraint_builtins_strings[cbi]);
    EXCEPT(e);
  }
  return 0;
}

static int constraint_compatible_assign_tag_each(const ident *tag,
                                                 cbool *value, void *user) {
  struct merge_state *st = user;

  cbool *existing = tagset_get(&st->target->tags, *tag);
  if (existing == NULL || *existing == U) {
    error e = mk_except_constraint(st->mod, st->for_error,
                                   "constraint does not allow"
                                   " possible case '%s' from",
                                   idents_value(st->mod->gctx, *tag));
    THROW(e);
  }

  return 0;
}

static ERROR constraint_check_compatible_assign(struct module *mod,
                                                const struct node *ntarget,
                                                const struct node *nc) {
  struct constraint *target = ntarget->constraint;
  struct constraint *c = CONST_CAST(nc->constraint);

  error e;
  for (size_t cbi = 0; cbi < CBI__NUM; ++cbi) {
    if (target->table[cbi] == Y && c->table[cbi] != Y) {
      e = mk_except_constraint(mod, nc,
                               "'%s' constraint on target"
                               " not satisfied by source",
                               constraint_builtins_strings[cbi]);
      EXCEPT(e);
    }
    if (target->table[cbi] == N && c->table[cbi] != N) {
      e = mk_except_constraint(mod, nc,
                               "'not %s' constraint on target"
                               " not satisfied by source",
                               constraint_builtins_strings[cbi]);
      EXCEPT(e);
    }
  }

  if (target->table[CBI__ANYTAG] == N) {
    if (c->table[CBI__ANYTAG] != N) {
      e = mk_except_constraint(mod, nc,
                               "constrained enum or union target"
                               " does not allow unconstrained"
                               " enum or union source");
      THROW(e);
    }

    struct merge_state st = {
      .mod = mod,
      .target = target,
      .for_error = nc,
    };
    e = tagset_foreach(&c->tags, constraint_compatible_assign_tag_each, &st);
    EXCEPT(e);
  }

  return 0;
}

EXAMPLE_NCC_EMPTY(constraint) {
  {
    struct node *na = node_new_subnode(mod, mod->body);
    struct node *nb = node_new_subnode(mod, mod->body);
    na->constraint = new_constraint(mod);
    nb->constraint = new_constraint(mod);
    constraint_set(mod, na->constraint, CBI_INIT, false);
    constraint_set(mod, nb->constraint, CBI_INIT, false);
    assert(0 == constraint_check_compatible_assign(mod, na, nb));
  }
  {
    struct node *na = node_new_subnode(mod, mod->body);
    struct node *nb = node_new_subnode(mod, mod->body);
    na->constraint = new_constraint(mod);
    nb->constraint = new_constraint(mod);
    constraint_set(mod, na->constraint, CBI_INIT, false);
    should_fail(constraint_check_compatible_assign(mod, na, nb));
  }
  {
    struct node *na = node_new_subnode(mod, mod->body);
    struct node *nb = node_new_subnode(mod, mod->body);
    na->constraint = new_constraint(mod);
    nb->constraint = new_constraint(mod);
    constraint_set(mod, na->constraint, CBI_INIT, false);
    constraint_set(mod, nb->constraint, CBI_NONNULL, false);
    should_fail(constraint_check_compatible_assign(mod, na, nb));
  }
  {
    struct node *na = node_new_subnode(mod, mod->body);
    struct node *nb = node_new_subnode(mod, mod->body);
    na->constraint = new_constraint(mod);
    nb->constraint = new_constraint(mod);
    constraint_set_tag(mod, na->constraint, ID_C, false);
    constraint_set_tag(mod, na->constraint, ID_CTOR, false);
    constraint_set_tag(mod, nb->constraint, ID_C, false);
    constraint_unset_tag(mod, na->constraint, ID_CTOR);
    constraint_set_tag(mod, na->constraint, ID_CTOR, false);
    assert(0 == constraint_check_compatible_assign(mod, na, nb));
  }
}

static ERROR reevaluate_wrt(struct module *mod,
                            struct constraint *c, const struct node *node) {
  const cbool t = node->constraint->table[CBI_EQTRUE];
  if (t == U) {
    return 0;
  }

  struct vecrel *eqs = &c->rels[CBI2_EQ];
  for (size_t n = 0, count = vecrel_count(eqs); n < count; ++n) {
    struct relation *eq = vecrel_get(eqs, n);
    if (eq->node == node) {
      constraint_set(mod, c, CBI_EQTRUE, t == N);
      break;
    }
  }

  struct hypothesis *hyp = hypmap_get(&c->under, node);
  if (hyp != NULL) {
    struct constraint *to_merge = NULL;
    if (t == Y) {
      to_merge = hyp->if_true;
      hyp->if_true = NULL;
    } else {
      to_merge = hyp->if_false;
      hyp->if_false = NULL;
    }

    if (to_merge != NULL) {
      constraint_merge_hypothesis(c, to_merge);
    }
  }

  return 0;
}

static ERROR constraint_inference_ident(struct module *mod, struct node *node) {
  const struct node *def = node->as.IDENT.def;
  if (def == NULL
      || (NM(def->which) & ( NM(IMPORT) | NM(MODULE) | NM(MODULE_BODY)))) {
    // noop
  } else {
    constraint_copy(mod, node->constraint, def->constraint);

    if (NM(def->which) & (NM(DEFNAME) | NM(DEFARG))) {
      error e = reevaluate_wrt(mod, node->constraint, def);
      EXCEPT(e);
    }
  }

  struct node *prev_use = node->as.IDENT.prev_use;
  if (prev_use != NULL) {
    constraint_copy(mod, node->constraint, prev_use->constraint);
  }
  return 0;
}

static int absurd_hypothesis_each(const struct node **cond,
                                  struct hypothesis *hyp, void *user) {
  struct node *node = user;
  struct constraint *c = node->constraint;

  if (hyp->if_true != NULL
      && hyp->if_true->table[CBI_EQTRUE] != U
        && hyp->if_true->table[CBI_EQTRUE] != c->table[CBI_EQTRUE]) {
      if (hyp->if_false != NULL) {
        constraint_merge_hypothesis(c, hyp->if_false);
        hyp->if_false = NULL;
      }
    }

  if (hyp->if_false != NULL
      && hyp->if_false->table[CBI_EQTRUE] != U
      && hyp->if_false->table[CBI_EQTRUE] != c->table[CBI_EQTRUE]) {
    if (hyp->if_true != NULL) {
      constraint_merge_hypothesis(c, hyp->if_true);
      hyp->if_true = NULL;
    }
  }

  // Not removing user.

  return 0;
}

static void reason_by_absurd(struct node *node) {
  struct constraint *c = node->constraint;
  if (c->table[CBI_EQTRUE] == U) {
    return;
  }

  int ret = hypmap_foreach(&c->under, absurd_hypothesis_each, node);
  assert(!ret);
}

static ERROR cond_descend_eval(struct module *mod, struct node *cond,
                               struct node *node, bool reversed);

static ERROR cond_descend_eval_bin(struct module *mod, struct node *cond,
                                   struct node *node, bool reversed) {
  struct node *na = subs_first(node);
  struct node *nb = subs_last(node);
  struct constraint *a = na->constraint;
  struct constraint *b = nb->constraint;
  enum token_type op = node->as.BIN.operator;

  if (OP_KIND(op) != OP_BIN_SYM_PTR && OP_KIND(op) != OP_BIN_SYM_BOOL) {
    return 0;
  }

  error e;
  bool operand_reversed;

  switch (op) {
  case Tand:
    e = cond_descend_eval(mod, cond, na, reversed);
    EXCEPT(e);
    e = cond_descend_eval(mod, cond, nb, reversed);
    EXCEPT(e);
    return 0;
  case Tor:
    // FIXME: unsupported.
    return 0;

  case TEQPTR:
    if (a->table[CBI_NONNULL] == b->table[CBI_NONNULL]) {
      // noop
    } else if (a->table[CBI_NONNULL] == N && b->table[CBI_NONNULL] == Y) {
      e = mk_except_constraint(mod, node, "unsatisfiable === comparison"
                               " lhs is 'not nonnull', rhs is 'nonnull'");
      THROW(e);
    } else if (a->table[CBI_NONNULL] == Y && b->table[CBI_NONNULL] == N) {
      e = mk_except_constraint(mod, node, "unsatisfiable === comparison"
                               " lhs is 'nonnull', rhs is 'not nonnull'");
      THROW(e);
    }
    operand_reversed = reversed;
    break;
  case TNEPTR:
    if (a->table[CBI_NONNULL] != b->table[CBI_NONNULL]) {
      // noop
    } else if (a->table[CBI_NONNULL] == N && b->table[CBI_NONNULL] == N) {
      e = mk_except_constraint(mod, node, "unsatisfiable !== comparison"
                               " lhs is 'not nonnull', rhs is 'not nonnull'");
      THROW(e);
    }
    operand_reversed = !reversed;
    break;
  default:
    assert(false);
    break;
  }

  assert(op == TEQPTR || op == TNEPTR);
  if (subs_last(node)->which == IDENT && subs_first(node)->which == IDENT) {
    constraint_set_rel(mod, subs_first(node), CBI2_EQ,
                       subs_last(node), operand_reversed);
    constraint_set_rel(mod, subs_first(node), CBI2_EQ,
                       subs_last(node), operand_reversed);
  }

  if (b->table[CBI_NONNULL] == U) {
    SWAP(a, b);
    SWAP(na, nb);
  }

  if (a->table[CBI_NONNULL] == U) {
    struct constraint *ca = assuming(mod, na, cond, reversed);
    switch (ca->table[CBI_NONNULL]) {
    case U:
      if (b->table[CBI_NONNULL] == Y) {
        constraint_set(mod, ca, CBI_NONNULL, operand_reversed);
      } else if (b->table[CBI_NONNULL] == N) {
        constraint_set(mod, ca, CBI_NONNULL, !operand_reversed);
      }
      break;
    case Y:
      if (b->table[CBI_NONNULL] == Y) {
        assert(operand_reversed);
      } else if (b->table[CBI_NONNULL] == N) {
        assert(!operand_reversed);
      }
      break;
    case N:
      if (b->table[CBI_NONNULL] == Y) {
        assert(!operand_reversed);
      } else if (b->table[CBI_NONNULL] == N) {
        assert(operand_reversed);
      }
      break;
    }
  }

  return 0;
}

static ERROR cond_descend_eval_un(struct module *mod, struct node *cond,
                                  struct node *node, bool reversed) {
  error e;
  switch (node->as.UN.operator) {
  case Tnot:
    e = cond_descend_eval(mod, cond, subs_first(node), !reversed);
    EXCEPT(e);
    break;
  default:
    break;
  }
  return 0;
}

// This assumes SSA form and therefore doesn't need to be recursive.
static ERROR cond_descend_eval(struct module *mod, struct node *cond,
                               struct node *node, bool reversed) {
  error e;
  switch (node->which) {
  case BIN:
    e = cond_descend_eval_bin(mod, cond, node, reversed);
    EXCEPT(e);
    break;
  case UN:
    e = cond_descend_eval_un(mod, cond, node, reversed);
    EXCEPT(e);
    break;
  case IDENT:
    {
      struct constraint *c = assuming(mod, node, cond, reversed);
      c->table[CBI_EQTRUE] = reversed ? N : Y;
    }
    break;
  default:
    break;
  }

  return 0;
}

static void constraint_inference_phi_conditioned_match_pattern(struct module *mod,
                                                               struct node *node) {
  struct node *pattern = prev(parent(node));
  if (node_ident(pattern) == ID_OTHERWISE) {
    pattern = prev(prev(pattern));
    while (pattern != NULL) {
      constraint_set_tag(mod, node->constraint, node_ident(pattern), true);
      pattern = prev(prev(pattern));
    }
  } else {
    constraint_set_tag(mod, node->constraint, node_ident(pattern), false);
  }
}

static void merge_branch_cond_assumption(struct module *mod,
                                         struct node *phi,
                                         struct node *cond,
                                         bool reversed) {
  assert(phi->which == PHI);
  struct constraint *c = phi->constraint;
  struct constraint *a = try_assuming(mod, phi, cond, reversed);
  if (a != NULL) {
    constraint_merge_hypothesis(c, a);
  }
}

static ERROR constraint_inference_phi_conditioned(struct module *mod,
                                                  struct node *phi) {
  assert(phi->as.PHI.is_conditioned);
  assert(vecancestor_count(&phi->as.PHI.ancestors) == 1);
  const struct ancestor *ancestor = vecancestor_get(&phi->as.PHI.ancestors, 0);
  constraint_copy(mod, phi->constraint, ancestor->prev->constraint);

  struct branch_state *br_st = mod->state->branch_state;
  struct node *branching = br_st->branching;
  struct node *cond = br_st->cond;
  const bool reversed = br_st->reversed;

  switch (branching->which) {
  case MATCH:
    constraint_inference_phi_conditioned_match_pattern(mod, phi);
    break;
  case IF:
    merge_branch_cond_assumption(mod, phi, cond, reversed);
    break;
  case WHILE:
    merge_branch_cond_assumption(mod, phi, cond, reversed);
    break;
  case TRY:
    // FIXME: unsupported
    break;
  default:
    assert(false);
    return 0;
  }

  return 0;
}

// This is inherently pessimistic. In general, we cannot know if a given set
// of branches is exhaustive. Yet a constraint can be added to a PHI only if
// the branches are unanimous.
//   let x
//   such
//     if i > 0
//       x = 0
//     elif i <= 0
//       x = 1
// We don't know that x::init in all cases, because we don't know that 'i>0
// or i<=0' is exhaustive. The LIR conversion adds an implicit else case in
// which x is not initialized, but we're unable to tell (in general) that
// the else case is redundant. N programmers should write the above thusly:
//   let x
//   such
//     if i > 0
//       x = 0
//     else
//       assert <- i <= 0
//       x = 1

// FIXME:
//  if cond
//    x ::not |A
//  else
//    x ::not |A
//  x ::()
//
//  Then 'not |A' is lost!
static int all_possible_tags_each(const ident *tag, cbool *value, void *user) {
  if (*value != Y) {
    return 0;
  }

  struct tagset *ctags = user;
  cbool *existing = tagset_get(ctags, *tag);
  if (existing != NULL) {
    *existing = Y;
  } else {
    tagset_set(ctags, *tag, Y);
  }

  return 0;
}

// FIXME: same as all_possible_tags_each()
static int unset_redundant_hypothesis_tag_each(const ident *tag, cbool *value,
                                               void *user) {
  if (*value != Y) {
    return 0;
  }

  struct tagset *ctags = user;
  cbool *existing = tagset_get(ctags, *tag);
  if (existing != NULL && *existing == *value) {
    *value = U;
  }

  return 0;
}

static void merge_unanimous_hypotheses(struct module *mod,
                                       struct constraint *c,
                                       struct node *cond) {
  cond = ident_expr_def(cond);

  struct hypothesis *hyp = hypmap_get(&c->under, cond);
  if (hyp == NULL) {
    return;
  }

  struct constraint *a = hyp->if_true;
  struct constraint *b = hyp->if_false;

  if (a == NULL || b == NULL) {
    return;
  }

  for (size_t cbi = 0; cbi < CBI__NUM; ++cbi) {
    if (a->table[cbi] != U && a->table[cbi] == b->table[cbi]) {
      c->table[cbi] = a->table[cbi];
      a->table[cbi] = U;
      b->table[cbi] = U;
    }
  }

  if (c->table[CBI__ANYTAG] == N) {
    tagset_destroy(&c->tags);
    tagset_init(&c->tags, 0);
    tagset_set_delete_val(&c->tags, U);
  }

  if (a->table[CBI__ANYTAG] == N && b->table[CBI__ANYTAG] == N) {
    a->table[CBI__ANYTAG] = N;
    tagset_foreach(&a->tags, all_possible_tags_each, &c->tags);
    tagset_foreach(&b->tags, all_possible_tags_each, &c->tags);

    tagset_foreach(&a->tags, unset_redundant_hypothesis_tag_each, &c->tags);
    tagset_foreach(&b->tags, unset_redundant_hypothesis_tag_each, &c->tags);
  } else if (a->table[CBI__ANYTAG] == Y || b->table[CBI__ANYTAG] == Y) {
    c->table[CBI__ANYTAG] = Y;
    a->table[CBI__ANYTAG] = U;
    b->table[CBI__ANYTAG] = U;
  }
}

unused__ static void ppanc(const struct module *mod, const struct ancestor *a) {
  fprintf(stderr, "reversed %d\n", a->reversed);
  if (a->prev) {
    fprintf(stderr, "prev: ");
    fflush(stderr);
    pptree(2, mod, a->prev);
  }
  if (a->cond) {
    fprintf(stderr, "cond: ");
    fflush(stderr);
    pptree(2, mod, a->cond);
  }
}

static ERROR constraint_inference_phi_propagation(struct module *mod, struct node *node) {
  error e = reevaluate_wrt(mod, node->constraint, node->as.PHI.propagation_of);
  EXCEPT(e);
  return 0;
}

static ERROR constraint_inference_phi(struct module *mod, struct node *node) {
  if (node->as.PHI.is_conditioned) {
    error e = constraint_inference_phi_conditioned(mod, node);
    EXCEPT(e);
    return 0;
  }

  if (node->as.PHI.propagation_of != NULL) {
    error e = constraint_inference_phi_propagation(mod, node);
    EXCEPT(e);
    return 0;
  }

  struct constraint *c = node->constraint;

  for (size_t n = 0, count = vecancestor_count(&node->as.PHI.ancestors);
       n < count; ++n) {
    const struct ancestor *ancestor = vecancestor_get(&node->as.PHI.ancestors, n);
    if (ancestor->cond != NULL) {
      struct constraint *a = assuming(mod, node, ancestor->cond, ancestor->reversed);
      constraint_copy(mod, a, ancestor->prev->constraint);
    } else {
      constraint_copy(mod, c, ancestor->prev->constraint);
    }
  }

  for (size_t n = 0, count = vecancestor_count(&node->as.PHI.ancestors);
       n < count; ++n) {
    const struct ancestor *ancestor = vecancestor_get(&node->as.PHI.ancestors, n);
    if (ancestor->cond != NULL) {
      merge_unanimous_hypotheses(mod, c, ancestor->cond);
    }
  }

  return 0;
}

static void insert_in_use_chain(struct node *phip, struct node *def) {
  struct phi_tracker_state *st = get_phi_tracker(def);
  struct node *prev = st->last;
  if (prev == NULL) {
    prev = def;
  } else {
    assert(prev->which == IDENT);
    struct node *next = prev->as.IDENT.next_use;
    next->as.IDENT.prev_use = phip;
    prev->as.IDENT.next_use = phip;

    for (size_t n = 0, count = vecancestor_count(&next->as.PHI.ancestors);
         n < count; ++n) {
      vecancestor_push(&phip->as.PHI.ancestors,
                       *vecancestor_get(&next->as.PHI.ancestors, n));
    }
    vecancestor_destroy(&next->as.PHI.ancestors);

    struct ancestor ancestor = { .prev = phip, 0 };
    vecancestor_push(&next->as.PHI.ancestors, ancestor);
  }

  struct ancestor ancestor = { .prev = prev, 0 };
  vecancestor_push(&phip->as.PHI.ancestors, ancestor);
}

static void propagate(struct module *mod, struct node *node) {
  struct constraint *c = node->constraint;
  if (c->table[CBI_EQTRUE] == U) {
    return;
  }

  struct node *after = node;

  struct constraint *dc = ident_expr_def(node)->constraint;
  for (size_t n = 0, count = vecnode_count(dc->users); n < count; ++n) {
    __break();
    struct node *u = *vecnode_get(dc->users, n);

    struct node *def = ident_expr_def(u);
    struct node *phip = mk_node(mod, parent(node), PHI);
    phip->as.PHI.def = def;
    phip->as.PHI.propagation_of = node;
    phip->as.PHI.is_used = true;
    phip->typ = TBI__NOT_TYPEABLE;

    insert_in_use_chain(phip, def);

    node_subs_remove(parent(node), phip);
    node_subs_insert_after(parent(node), after, phip);
    after = phip;

    error e = catchup(mod, NULL, phip, CATCHUP_AFTER_CURRENT);
    assert(!e);
  }
}

static bool constraint_has_common_root_tag(ident *tag,
                                           const struct module *mod,
                                           const struct node *node) {
  size_t count = tagset_count(&node->constraint->tags);
  if (count == 0) {
    return false;
  } else if (count == 1) {
    error e = constraint_get_single_tag(tag, mod, node);
    assert(!e);
    return true;
  } else {
    return false;
  }
}

static void constraint_defchoice_container(const struct node **container,
                                           struct module *mod, struct node *node) {
  struct node *par = subs_first(node);
  assert(par->flags & NODE_IS_DEFCHOICE);

  ident common = ID__NONE;
  if (constraint_has_common_root_tag(&common, mod, par)) {
    *container = node_get_member_const(typ_definition_nooverlay_const(par->typ),
                                       common);
  } else {
    *container = typ_definition_nooverlay_const(par->typ);
  }
}

static ERROR constraint_inference_bin_acc(struct module *mod,
                                          struct node *node) {
  struct node *base = subs_first(node);
  struct node *name = subs_last(node);

  error e;
  if (!(base->flags & NODE_IS_TYPE)) {
    e = constraint_check(mod, base, CBI_INIT, false);
    EXCEPT(e);
  }

  struct node *field = NULL;
  const struct node *container;
  if (base->flags & NODE_IS_DEFCHOICE) {
    constraint_defchoice_container(&container, mod, node);
  } else if (typ_is_reference(base->typ)) {
    container = typ_definition_nooverlay_const(typ_generic_arg(base->typ, 0));
  } else {
    container = typ_definition_nooverlay_const(base->typ);
  }
  e = scope_lookup_ident_immediate(&field, name, mod, container,
                                   node_ident(name), false);
  EXCEPT(e);
  if (NM(field->which) & ( NM(IMPORT) | NM(MODULE) | NM(MODULE_BODY) )) {
    // noop
  } else {
    constraint_copy(mod, node->constraint, field->constraint);
  }

  if (node->flags & NODE_IS_DEFCHOICE) {
    if (parent(node)->which == TYPECONSTRAINT
        && subs_first(parent(node))->which == INIT) {
      // noop
    } else if ((base->flags & NODE_IS_TYPE)
               && container->which == DEFTYPE
               && container->as.DEFTYPE.kind == DEFTYPE_ENUM) {
      // noop
    } else {
      if (constraint_get_tag(base->constraint, node_ident(name)) != Y) {
        e = mk_except_constraint(mod, base, "cannot access tag '%s'"
                                 " with these constraints",
                                 idents_value(mod->gctx, node_ident(name)));
        THROW(e);
      }
    }

    constraint_set_tag(mod, node->constraint, node_ident(name), false);
  }

  if (base->constraint->table[CBI_INIT] != U) {
    constraint_set(mod, node->constraint, CBI_INIT,
                   base->constraint->table[CBI_INIT] == N);
  }

  if (field->which == DEFFIELD
      && typ_is_reference(field->typ)
      && !typ_isa(field->typ, TBI_ANY_NREF)
      && base->constraint->table[CBI_INIT] != U) {
    constraint_set(mod, node->constraint, CBI_NONNULL,
                   base->constraint->table[CBI_NONNULL] == N);
  }

  return 0;
}

static void constraint_inference_assign_like(struct module *mod, struct node *node) {
  assert(NM(node->which) & (NM(DEFNAME) | NM(BIN)));
  struct node *dst = node->which == DEFNAME ? node : subs_first(node);
  struct node *src = subs_last(node);

  switch (src->which) {
  case IDENT:
    constraint_set_rel(mod, dst, CBI2_EQ, src, false);
    constraint_set_rel(mod, src, CBI2_EQ, dst, false);
    break;
  case BOOL:
    constraint_set(mod, dst->constraint, CBI_EQTRUE, !src->as.BOOL.value);
    break;
  default:
    break;
  }
}

static ERROR constraint_inference_bin(struct module *mod,
                                      struct node *node) {
  error e;
  enum token_type op = node->as.BIN.operator;
  switch (OP_KIND(op)) {
  case OP_BIN:
  case OP_BIN_RHS_TYPE:
    return 0;
  case OP_BIN_SYM:
    if (OP_IS_ASSIGN(op)) {
      e = constraint_check(mod, subs_last(node), CBI_INIT, false);
      EXCEPT(e);
      constraint_copy(mod, subs_first(node)->constraint,
                      subs_last(node)->constraint);
      constraint_set(mod, node->constraint, CBI_INIT, false);

      if (op == TASSIGN) {
        constraint_inference_assign_like(mod, node);
      }
      return 0;
    }
    // fallthrough
  case OP_BIN_SYM_BOOL:
  case OP_BIN_SYM_ADDARITH:
  case OP_BIN_SYM_ARITH:
  case OP_BIN_SYM_INTARITH:
  case OP_BIN_SYM_OVARITH:
  case OP_BIN_SYM_BW:
  case OP_BIN_SYM_PTR:
  case OP_BIN_BW_RHS_UNSIGNED:
  case OP_BIN_OVBW_RHS_UNSIGNED:
    e = constraint_check(mod, subs_first(node), CBI_INIT, false);
    EXCEPT(e);
    e = constraint_check(mod, subs_last(node), CBI_INIT, false);
    EXCEPT(e);
    constraint_set(mod, node->constraint, CBI_INIT, false);
    return 0;
  case OP_BIN_ACC:
    e = constraint_inference_bin_acc(mod, node);
    EXCEPT(e);
    return 0;
  default:
    assert(false);
    break;
  }
  return 0;
}

static ERROR constraint_inference_un(struct module *mod,
                                     struct node *node) {
  error e;
  switch (OP_KIND(node->as.UN.operator)) {
  case OP_UN_SLICE:
    constraint_set(mod, node->constraint, CBI_INIT, false);
    return 0;
  case OP_UN_PRIMITIVES:
    if (node->as.UN.operator == T__NULLABLE) {
      constraint_copy(mod, node->constraint, subs_first(node)->constraint);
    }
    return 0;
  case OP_UN_REFOF:
    switch (node->as.UN.operator) {
    case TNULREFDOT:
    case TNULREFBANG:
    case TNULREFSHARP:
    case TNULREFWILDCARD:
      break;
    default:
      constraint_set(mod, node->constraint, CBI_NONNULL, false);
      break;
    }
    constraint_set(mod, node->constraint, CBI_INIT, false);
    return 0;
  case OP_UN_DEREF:
    e = constraint_check(mod, subs_first(node), CBI_INIT, false);
    EXCEPT(e);
    e = constraint_check(mod, subs_first(node), CBI_NONNULL, false);
    EXCEPT(e);
    constraint_set(mod, node->constraint, CBI_INIT, false);
    if (typ_is_reference(node->typ) && !typ_isa(node->typ, TBI_ANY_NREF)) {
      constraint_set(mod, node->constraint, CBI_NONNULL, false);
    }
    return 0;
  case OP_UN_BOOL:
  case OP_UN_ADDARITH:
  case OP_UN_OVARITH:
  case OP_UN_BW:
    e = constraint_check(mod, subs_first(node), CBI_INIT, false);
    EXCEPT(e);
    constraint_set(mod, node->constraint, CBI_INIT, false);
    return 0;
  default:
    assert(false);
    break;
  }
  return 0;
}

static ERROR constraint_inference_tuples(struct module *mod,
                                         struct node *node) {
  // FIXME
  return 0;
}

static ERROR constraint_inference_call(struct module *mod,
                                       struct node *node) {
  if (node->flags & NODE_IS_TYPE) {
    constraint_copy(mod, node->constraint,
                    typ_definition_nooverlay_const(node->typ)->constraint);
    return 0;
  }

  error e;
  const struct node *fun = subs_first(node);
  const struct typ *tfun = fun->typ;
  const struct node *dfun = typ_definition_nooverlay_const(tfun);

  const struct node *funargs = subs_at_const(dfun, IDX_FUNARGS);
  const ssize_t first_vararg = node_fun_first_vararg(dfun);
  ssize_t n = 0;
  FOREACH_SUB_EVERY(arg, node, 1, 1) {
    if (n == first_vararg) {
      break;
    }
    const struct node *target = subs_at_const(funargs, n);
    e = constraint_check_compatible_assign(mod, target, arg);
    EXCEPT(e);
    n += 1;
  }

  if (n == first_vararg) {
    const struct node *target = subs_at_const(funargs, n);

    FOREACH_SUB_EVERY(arg, node, 1 + n, 1) {
      e = constraint_check_compatible_assign(mod, target, arg);
      EXCEPT(e);
    }
  }

  const struct node *ret = subs_at_const(funargs, n);
  constraint_copy(mod, node->constraint, ret->constraint);

  return 0;
}

static ERROR constraint_inference_return(struct module *mod,
                                         struct node *node) {
  const struct node *ret = module_retval_get(mod);
  if (!typ_equal(ret->typ, TBI_VOID)) {
    error e = constraint_check_compatible_assign(mod, ret, subs_first(node));
    EXCEPT(e);
  }

  return 0;
}

//static int check_tag_is_set_each(const ident *name, cbool *value, void *user) {
//  struct tagset *right_tags = user;
//  if (*value == Y) {
//    const cbool *existing = tagset_get(right_tags, *name);
//    if (existing == NULL || *existing == N) {
//      return *name;
//    }
//  }
//  return 0;
//}
//
//static int restrict_tags_each(const ident *name, cbool *value, void *user) {
//  if (*value == U) {
//    return 0;
//  }
//
//  struct tagset *ctags = user;
//  cbool *existing = tagset_get(ctags, *name);
//  if (existing == NULL) {
//    tagset_set(ctags, *name, *value);
//  } else if (*existing == U) {
//    *existing = *value;
//  } else if (*value == U) {
//    // noop
//  } else if (*existing != *value) {
//    return *name;
//  }
//  return 0;
//}
//
//static error constraint_inference_typeconstraint(struct module *mod,
//                                                 struct node *node) {
//  struct constraint *c = node->constraint;
//  struct node *nleft = subs_first(node);
//  struct constraint *left = nleft->constraint;
//  struct node *nright = subs_last(node);
//  struct constraint *right = nright->constraint;
//  error e;
//
//  constraint_copy(mod, node->constraint, left);
//
//  if (right->table[CBI__ANYTAG] == N && c->table[CBI__ANYTAG] == N) {
//    const int ret = tagset_foreach(&c->tags, check_tag_is_set_each,
//                                   &right->tags);
//    if (ret != 0) {
//      const ident failed = ret;
//      e = mk_except_constraint(mod, nright,
//                               "constraint on tag '%s' is a restriction",
//                               idents_value(mod->gctx, failed));
//      EXCEPT(e);
//    }
//  }
//
//  for (size_t cbi = 0; cbi < CBI__NUM; ++cbi) {
//    if (c->table[cbi] == U) {
//      c->table[cbi] = right->table[cbi];
//    } else if (right->table[cbi] == U) {
//      // noop
//    } else if (c->table[cbi] != right->table[cbi]) {
//      e = mk_except_constraint(mod, nright,
//                               "incompatible constraint '::%s'",
//                               constraint_builtins_strings[cbi]);
//      THROW(e);
//    }
//  }
//
//  if (right->table[CBI__ANYTAG] == N) {
//     const int ret = tagset_foreach(&right->tags, restrict_tags_each, &c->tags);
//     assert(ret == 0);
//  }
//
//  // Copy over to LHS as the TYPECONSTRAINT itself will get elided in
//  // step_remove_typeconstraints().
//  constraint_copy(mod, left, c);
//  return 0;
//}

static ERROR constraint_inference_defname(struct module *mod,
                                          struct node *node) {
  assert(node->which == DEFNAME);

  node->constraint->users = calloc(1, sizeof(*node->constraint->users));

  constraint_copy(mod, node->constraint, subs_last(node)->constraint);
  if (typ_isa(node->typ, TBI_TRIVIAL_CTOR)) {
    constraint_set(mod, node->constraint, CBI_INIT, false);
  }

  if (typ_equal(node->typ, TBI_BOOL)) {
    error e = cond_descend_eval(mod, node, subs_last(node), false);
    EXCEPT(e);

    e = cond_descend_eval(mod, node, subs_last(node), true);
    EXCEPT(e);
  }

  constraint_inference_assign_like(mod, node);

  return 0;
}

static ERROR constraint_inference_defarg(struct module *mod,
                                         struct node *node) {
  node->constraint->users = calloc(1, sizeof(*node->constraint->users));
  constraint_copy(mod, node->constraint, subs_last(node)->constraint);
  constraint_set(mod, node->constraint, CBI_INIT, false);
  return 0;
}

static ERROR constraint_inference_deffield(struct module *mod,
                                           struct node *node) {
  return 0;
}

static ERROR constraint_inference_defchoice(struct module *mod,
                                            struct node *node) {
  if (!node->as.DEFCHOICE.has_payload) {
    constraint_set(mod, node->constraint, CBI_INIT, false);
  }
  return 0;
}

static ERROR constraint_inference_definitions(struct module *mod,
                                              struct node *node) {
  if (typ_is_reference(node->typ) && !typ_is_nullable_reference(node->typ)) {
    constraint_set(mod, node->constraint, CBI_NONNULL, false);
  }
  return 0;
}

static ERROR constraint_inference_genarg(struct module *mod,
                                         struct node *node) {
  if (typ_is_reference(node->typ) && !typ_is_nullable_reference(node->typ)) {
    constraint_set(mod, node->constraint, CBI_NONNULL, false);
  }
  return 0;
}

static ERROR evaluate(struct module *mod, struct node *node) {
  struct constraint *c = node->constraint;
  for (size_t n = 0, count = vecrel_count(&c->rels[CBI2_EQ]); n < count; ++n) {
    struct relation *rel = vecrel_get(&c->rels[CBI2_EQ], n);
    if (rel->value == U) {
      continue;
    }
    struct constraint *o = rel->node->constraint;
    if (o->table[CBI_EQTRUE] == U) {
      continue;
    }

    const cbool cv = c->table[CBI_EQTRUE];
    const cbool ov = o->table[CBI_EQTRUE];
    if (cv == U) {
      c->table[CBI_EQTRUE] = ov;
    } else if (cv != ov) {
      error e = mk_except_constraint(mod, node, "constrained to be EQ to '%s'"
                                     " which is %sEQTRUE, but is:",
                                     scope_name(mod, rel->node),
                                     ov == N ? "not " : "");
      THROW(e);
    }
  }
  return 0;
}

static bool within_tentative_context(struct module *mod) {
  const struct node *top = mod->state->top_state->top;
  return typ_is_tentative(top->typ)
    || (node_is_at_top(parent_const(top)) && typ_is_tentative(parent_const(top)->typ));
}

STEP_NM(step_constraint_inference, -1);
error step_constraint_inference(struct module *mod, struct node *node,
                                void *user, bool *stop) {
  // FIXME: improve and reenable.
  return 0;

  DSTEP(mod, node);
  error e;

  if (mod->state->top_state == NULL) {
    return 0;
  }

  if (within_tentative_context(mod)) {
    return 0;
  }

  if (node->typ == TBI__NOT_TYPEABLE && node->which != PHI) {
    return 0;
  }

  node->constraint = new_constraint(mod);

  switch (node->which) {
  case NIL:
    constraint_set(mod, node->constraint, CBI_INIT, false);
    constraint_set(mod, node->constraint, CBI_NONNULL, true);
    break;
  case IDENT:
    e = constraint_inference_ident(mod, node);
    EXCEPT(e);
    break;
  case PHI:
    e = constraint_inference_phi(mod, node);
    EXCEPT(e);
    break;
  case NUMBER:
  case BOOL:
  case STRING:
  case SIZEOF:
  case ALIGNOF:
    constraint_set(mod, node->constraint, CBI_INIT, false);
    break;
  case INIT:
    if (!(node->flags & NODE_IS_TYPE) && typ_isa(node->typ, TBI_TRIVIAL_CTOR)) {
      constraint_set(mod, node->constraint, CBI_INIT, false);
    }
    if (node->as.INIT.for_tag != ID__NONE) {
      constraint_set_tag(mod, node->constraint, node->as.INIT.for_tag, false);

      if (typ_definition_which(node->typ) == DEFTYPE
          && typ_definition_deftype_kind(node->typ) == DEFTYPE_ENUM) {
        constraint_set(mod, node->constraint, CBI_INIT, false);
      } else if (typ_definition_which(node->typ) == DEFTYPE
                 && typ_definition_deftype_kind(node->typ) == DEFTYPE_UNION) {
        const struct node *ch = node_get_member_const(node, node->as.INIT.for_tag);
        const struct node *ext = node_defchoice_external_payload(ch);
        if (ext != NULL && typ_isa(ext->typ, TBI_TRIVIAL_CTOR)) {
          constraint_set(mod, node->constraint, CBI_INIT, false);
        }
        // If the payload is inline (i.e. not external), we don't currently
        // have a way to determine that this particular payload, for this
        // tag, is trivial to build. We rely on whether the whole node->typ
        // is TBI_TRIVIAL_CTOR.
      } else {
        assert(false);
      }
    }
    break;
  case BIN:
    e = constraint_inference_bin(mod, node);
    EXCEPT(e);
    break;
  case UN:
    e = constraint_inference_un(mod, node);
    EXCEPT(e);
    break;
  case TUPLE:
    e = constraint_inference_tuples(mod, node);
    EXCEPT(e);
    break;
  case CALL:
    e = constraint_inference_call(mod, node);
    EXCEPT(e);
    break;
  case CALLNAMEDARG:
    constraint_copy(mod, node->constraint, subs_first(node)->constraint);
    break;
  case RETURN:
    e = constraint_inference_return(mod, node);
    EXCEPT(e);
    break;
  case BLOCK:
  case CATCH:
    {
      struct node *not_phi = subs_last(node);
      while (not_phi->which == PHI) {
        not_phi = prev(not_phi);
      }
      constraint_copy(mod, node->constraint, not_phi->constraint);
      constraint_copy(mod, node->constraint, not_phi->constraint);
    }
    break;
  case IF:
  case WHILE:
  case MATCH:
  case TRY:
    break;
  case CONV:
    constraint_copy(mod, node->constraint, subs_first(node)->constraint);
    break;
  case DEFALIAS:
    constraint_copy(mod, node->constraint, subs_last(node)->constraint);
    break;
  case DEFNAME:
    e = constraint_inference_defname(mod, node);
    EXCEPT(e);
    break;
  case DEFARG:
    e = constraint_inference_defarg(mod, node);
    EXCEPT(e);
    break;
  case DEFFIELD:
    e = constraint_inference_deffield(mod, node);
    EXCEPT(e);
    break;
  case DEFCHOICE:
    e = constraint_inference_defchoice(mod, node);
    EXCEPT(e);
    break;
  case DIRECTDEF:
    {
      const struct node *def = typ_definition_nooverlay_const(node->as.DIRECTDEF.typ);
      constraint_copy(mod, node->constraint, def->constraint);
    }
    break;
  case DEFINTF:
  case DEFTYPE:
    e = constraint_inference_definitions(mod, node);
    EXCEPT(e);
    break;
  case DEFGENARG:
  case SETGENARG:
    e = constraint_inference_genarg(mod, node);
    EXCEPT(e);
    break;
  case WITHIN:
    constraint_set(mod, node->constraint, CBI_INIT, false);
    constraint_set(mod, node->constraint, CBI_NONNULL, false);
    break;
  case FOR:
  case BREAK:
  case JUMP:
  case CONTINUE:
  case TYPECONSTRAINT:
  case NOOP:
  case EXCEP:
  case THROW:
  case DEFFUN:
  case DEFMETHOD:
  case DEFINCOMPLETE:
  case FUNARGS:
  case GENARGS:
  case LET:
  case PRE:
  case POST:
  case INVARIANT:
  case ISALIST:
  case ISA:
  case IMPORT:
  case MODULE:
  case MODULE_BODY:
  case ROOT_OF_ALL:
    // noop
    break;
  default:
    assert(false);
    break;
  }

  e = evaluate(mod, node);
  EXCEPT(e);

  reason_by_absurd(node);

  propagate(mod, node);

  return 0;
}

struct check_tag_state {
  struct module *mod;
  struct node *node;
  struct tagset *matched;
};

static int check_tag_is_matched_each(const ident *name, cbool *value,
                                     void *user) {
  struct check_tag_state *st = user;
  if (tagset_get(st->matched, *name) == NULL) {
    error e = mk_except_type(st->mod, st->node,
                             "non-exhaustive match, for example '%s'",
                             idents_value(st->mod->gctx, *name));
    EXCEPT(e);
  }
  return 0;
}

STEP_NM(step_check_exhaustive_match,
        NM(MATCH));
error step_check_exhaustive_match(struct module *mod, struct node *node,
                                  void *user, bool *stop) {
  // FIXME: improve and reenable.
  return 0;

  struct node *expr = subs_first(node);
  enum deftype_kind kind = typ_definition_deftype_kind(expr->typ);
  const bool enum_or_union = kind == DEFTYPE_ENUM || kind == DEFTYPE_UNION;

  if (!enum_or_union) {
    return 0;
  }

  struct tagset matched;
  tagset_init(&matched, 0);
  tagset_set_delete_val(&matched, U);

  error e = 0;
  FOREACH_SUB_EVERY(p, node, 1, 2) {
    ident id;
    switch (p->which) {
    case IDENT:
      id = node_ident(p);
      if (id == ID_OTHERWISE) {
        if (p != prev(subs_last(node))) {
          e = mk_except(mod, p, "default pattern '_' must be last");
          GOTO_THROW(e);
        }
        // No need to check further.
        goto ok;
      }
      break;
    case BIN:
      assert(OP_KIND(p->as.BIN.operator) == OP_BIN_ACC);
      id = node_ident(subs_at(p, 1));
      break;
    default:
      assert(false);
    }

    if (tagset_get(&matched, id) != NULL) {
      e = mk_except(mod, p, "duplicated match case");
      GOTO_THROW(e);
    }

    tagset_set(&matched, id, Y);
  }

  struct check_tag_state st = {
    .mod = mod,
    .node = node,
    .matched = &matched,
  };
  e = (error) tagset_foreach(&expr->constraint->tags,
                             check_tag_is_matched_each, &st);
  GOTO_EXCEPT(e);

ok:
except:
  tagset_destroy(&matched);
  return e;
}

STEP_NM(step_stop_generic_functor,
        STEP_NM_DEFS);
error step_stop_generic_functor(struct module *mod, struct node *node,
                                void *user, bool *stop) {
  DSTEP(mod, node);

  *stop = typ_is_generic_functor(node->typ);

  return 0;
}
