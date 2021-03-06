#include "autointf.h"

#include "types.h"
#include "passes.h"
#include "parser.h"
#include <ctype.h>

struct intf_proto_rewrite_state {
  struct typ *thi;
  const struct typ *proto_parent;
};

static STEP_NM(step_rewrite_this,
               NM(IDENT));
static ERROR step_rewrite_this(struct module *mod, struct node *node,
                               void *user, bool *stop) {
  struct typ *thi = user;
  ident id = node_ident(node);
  if (id == ID_THIS) {
    node_set_which(node, DIRECTDEF);
    set_typ(&node->as.DIRECTDEF.typ, thi);
    node->as.DIRECTDEF.flags = NODE_IS_TYPE;
  }
  return 0;
}

static STEP_NM(step_rewrite_local_idents,
               NM(DEFARG) | NM(IDENT));
static ERROR step_rewrite_local_idents(struct module *mod, struct node *node,
                                       void *user, bool *stop) {
  const struct typ *proto_parent = user;
  if (proto_parent == NULL) {
    return 0;
  }

  if (node->which == DEFARG) {
    subs_first(node)->typ = TBI__NOT_TYPEABLE;
    return 0;
  }
  if (node->typ == TBI__NOT_TYPEABLE) {
    return 0;
  }

  if (node->which == IDENT && parent_const(node)->which == BIN) {
    return 0;
  }

  const ident id = node_ident(node);
  if (id == ID_THIS || id == ID_FINAL) {
    return 0;
  }

  struct tit *m = typ_definition_one_member(proto_parent, id);
  if (m == NULL) {
    return 0;
  }

  if (NM(tit_which(m)) & (NM(DEFMETHOD) | NM(DEFFUN))) {
    return 0;
  }

  node_set_which(node, DIRECTDEF);
  set_typ(&node->as.DIRECTDEF.typ, tit_typ(m));

  tit_next(m);
  return 0;
}

static STEP_NM(step_rewrite_codeloc,
               -1);
static ERROR step_rewrite_codeloc(struct module *mod, struct node *node,
                                  void *user, bool *stop) {
  node->codeloc = parent_const(node)->codeloc;
  return 0;
}

static ERROR pass_rewrite_proto(struct module *mod, struct node *root,
                                void *user, ssize_t shallow_last_up) {
  PASS(DOWN_STEP(step_rewrite_this);
       DOWN_STEP(step_rewrite_local_idents);
       DOWN_STEP(step_rewrite_codeloc);
       ,,);
  return 0;
}

static void intf_proto_deepcopy(struct module *mod,
                                struct node *dst, const struct tit *src,
                                const struct typ *thi) {
  node_deepcopy_omit_tail_block(mod, dst, tit_node_ignore_any_overlay(src));

  PUSH_STATE(mod->state->step_state);
  error e = pass_rewrite_proto(mod, dst, (void *)thi, -1);
  assert(!e);
  POP_STATE(mod->state->step_state);

  if (node_toplevel(dst) != NULL) {
    node_toplevel(dst)->passing = -1;
    node_toplevel(dst)->passed = -1;
  }
}

static struct node *define_builtin_start(struct module *mod, struct node *deft,
                                         const struct typ *intf,
                                         const struct tit *mi) {
  struct node *d = node_new_subnode(mod, deft);
  intf_proto_deepcopy(mod, d, mi, intf);
  d->codeloc = deft->codeloc;

  struct toplevel *toplevel = node_toplevel(d);
  toplevel->flags &= ~TOP_IS_PROTOTYPE;
  toplevel->flags |= node_toplevel(deft)->flags & (TOP_IS_INLINE);

  return d;
}

static void define_builtin_catchup(struct module *mod, struct node *d) {
  error e = catchup(mod, NULL, d, CATCHUP_BELOW_CURRENT);
  assert(!e);
}

static enum token_type arg_ref_operator(const struct node *arg) {
  assert(arg->which == DEFARG);
  const struct node *tspec = subs_last_const(arg);
  assert(tspec->which == UN);
  return tspec->as.UN.operator;
}

static enum token_type arg_ref_accessor(const struct node *arg) {
  switch (arg_ref_operator(arg)) {
  case TREFDOT: return TDOT;
  case TREFBANG: return TBANG;
  case TREFSHARP: return TSHARP;
  default:
                  assert(false);
                  return 0;
  }
}

static const struct typ *corresponding_trivial(ident m) {
  switch (m) {
  case ID_CTOR:
    return TBI_TRIVIAL_CTOR;
  case ID_DTOR:
    return TBI_TRIVIAL_DTOR;
  default:
    return NULL;
  }
}

static void like_arg(struct module *mod, struct node *par,
                     const struct node *arg, const struct node *f) {
  GSTART();

  const struct node *parf = parent_const(f);
  if (parf->which == DEFCHOICE) {
    G0(b, par, BIN,
       b->as.BIN.operator = arg_ref_accessor(arg);
       G(b2, BIN,
         b2->as.BIN.operator = arg_ref_accessor(arg);
         G(base, IDENT,
           base->as.IDENT.name = node_ident(arg));
         G(a2, IDENT,
           a2->as.IDENT.name = node_ident(parf)));
       G(a, IDENT,
         a->as.IDENT.name = node_ident(f)));
  } else {
    G0(b, par, BIN,
       b->as.BIN.operator = arg_ref_accessor(arg);
       b->as.BIN.is_generated = true; // Maybe we're generating a method of Tuple_x
       G(base, IDENT,
         base->as.IDENT.name = node_ident(arg));
       G(a, IDENT,
         a->as.IDENT.name = node_ident(f)));
  }
}

bool is_abstract_ref_not_bothering_with_member(const struct typ *t, const ident member) {
  const struct typ *t0 = typ_generic_functor_const(t);
  return (t0 != NULL
      && (member == ID_DTOR || member == ID_MOVE || member == ID_COPY_CTOR)
      && (typ_equal(t0, TBI_ANY_REF)
          || typ_equal(t0, TBI_ANY_MREF)
          || typ_equal(t0, TBI_ANY_NREF)
          || typ_equal(t0, TBI_ANY_NMREF)
          || typ_equal(t0, TBI_ANY_LOCAL_REF)
          || typ_equal(t0, TBI_ANY_LOCAL_MREF)
          || typ_equal(t0, TBI_ANY_LOCAL_NREF)
          || typ_equal(t0, TBI_ANY_LOCAL_NMREF)));
}

static void gen_on_field(struct module *mod, struct node *m,
                         struct node *body, struct node *f,
                         struct typ *ftyp) {
  const ident mident = node_ident(m);
  if ((mident == ID_CTOR || mident == ID_DTOR) && typ_is_reference(ftyp)) {
    // Not deep operations.
    return;
  }

  if (is_abstract_ref_not_bothering_with_member(ftyp, mident)) {
    // In a very particular case: uninstantiated generic structs with
    // type arguments of the form (`Any_n?{,m,mm}ref T), as these intf are
    // shared between uncounted local references (which are trivial dtor,
    // move, and copy) and local counted references (which are not:
    // therefore the `Any_n?{,m,mm}ref cannot be trivial here either),
    // autointf would try to generate Dtor, Move, and Copy_ctor methods for
    // such structs. The body of these methods is purely abstract, but it
    // will be type-checked.  And in the process we may look for the Dtor,
    // Move, and Copy_ctor methods on uncounted local references. But for
    // bootstrapping reasons (we don't want to need references to type
    // references), these methods do not exists on uncounted local
    // references.
    //
    // So we skip fields of this kind, in purely abstract code, never
    // codegen.
    return;
  }

  GSTART();
  const struct node *funargs = subs_at_const(m, IDX_FUNARGS);
  const struct node *arg_self = subs_first_const(funargs);

  if (node_ident(m) == ID_COPY_CTOR || node_ident(m) == ID_MOVE) {
    const struct node *arg_other = next_const(arg_self);

    if (typ_is_optional(ftyp)) {
      G0(xif, body, IF,
         G(checkother, UN,
           checkother->as.BIN.operator = TPOSTQMARK;
           if (node_ident(m) == ID_MOVE) {
             like_arg(mod, checkother, arg_self, f);
           } else {
             like_arg(mod, checkother, arg_other, f);
           });
         G(block, BLOCK);
         G(noblock, BLOCK,
           G(noop, NOOP)));
      body = block;
    }

    if (node_ident(m) == ID_MOVE) {
      if (typ_isa(ftyp, TBI_TRIVIAL_MOVE)) {
        G0(assign, body, BIN,
           assign->as.BIN.operator = TASSIGN;
           G(rf, BIN,
             rf->as.BIN.operator = TSHARP;
             rf->as.BIN.is_generated = true; // Maybe we're generating a method of Tuple_x
             G(r, IDENT,
               r->as.IDENT.name = node_ident(arg_other));
             G(rfn, IDENT,
               rfn->as.IDENT.name = node_ident(f)));
           like_arg(mod, assign, arg_self, f));
      } else {
        G0(assign, body, BIN,
           assign->as.BIN.operator = TASSIGN;
           G(rf, BIN,
             rf->as.BIN.operator = TSHARP;
             rf->as.BIN.is_generated = true; // Maybe we're generating a method of Tuple_x
             G(r, IDENT,
               r->as.IDENT.name = node_ident(arg_other));
             G(rfn, IDENT,
               rfn->as.IDENT.name = node_ident(f)));
           G(move, BIN,
             move->as.BIN.operator = TSHARP;
             move->as.BIN.is_operating_on_counted_ref = true;
             like_arg(mod, move, arg_self, f);
             G_IDENT(movename, "Move")));
      }
    } else {
      G0(assign, body, BIN,
         assign->as.BIN.operator = TASSIGN;
         assign->as.BIN.is_operating_on_counted_ref = true;
         like_arg(mod, assign, arg_self, f);
         like_arg(mod, assign, arg_other, f));
    }
    return;
  }

  const struct typ *trivial = corresponding_trivial(node_ident(m));
  if (trivial != NULL && typ_isa(ftyp, trivial)) {
    return;
  }

  if (node_ident(m) == ID_CTOR || node_ident(m) == ID_DTOR) {
    G0(call, body, CALL,
       G(fun, BIN,
         fun->as.BIN.operator = arg_ref_accessor(arg_self);
         fun->as.BIN.is_operating_on_counted_ref = true;
         like_arg(mod, fun, arg_self, f);
         G(name, IDENT,
           name->as.IDENT.name = node_ident(m))));
    return;
  }

  assert(node_ident(m) == ID_OPERATOR_EQ);
  const struct node *arg_other = next_const(arg_self);

  assert(!typ_is_reference(ftyp));

  G0(xif, body, IF,
     G(cond, BLOCK);
     G(yes, BLOCK,
       G(ret, RETURN,
         G(fal, BOOL,
           fal->as.BOOL.value = false)));
     G(no, BLOCK,
       G(noop, NOOP)));
  body = cond;

  if (typ_is_optional(ftyp)) {
    const struct node *arg_other = next_const(arg_self);
    G0(checkand2, body, BIN,
       checkand2->as.BIN.operator = Tand;
       G(checkand, BIN,
         checkand->as.BIN.operator = Tand;
         G(checkother, UN,
           checkother->as.BIN.operator = TPOSTQMARK;
           like_arg(mod, checkother, arg_other, f));
         G(checkself, UN,
           checkself->as.BIN.operator = TPOSTQMARK;
           like_arg(mod, checkself, arg_self, f)));
       G(block, BLOCK));
    body = block;
  }

  G0(cmp, body, BIN,
     cmp->as.BIN.operator = typ_is_reference(ftyp) ? TNEPTR : TNE;
     like_arg(mod, cmp, arg_self, f);
     like_arg(mod, cmp, arg_other, f));
}

static void gen_on_choices_and_fields(struct module *mod,
                                      struct node *node,
                                      struct node *m,
                                      struct node *body) {
  GSTART();

  // TODO(e): Doesn't handle hiearchical unions.

  const struct node *funargs = subs_at_const(m, IDX_FUNARGS);
  const size_t arity = subs_count(funargs);
  bool need_true = false;

  if (node->which == DEFFIELD) {
    gen_on_field(mod, m, body, node, node->typ);
    return;
  } else if (node->which == DEFTYPE) {
    G0(noop, body, NOOP); // Ensure not empty.

    if (node->as.DEFTYPE.kind == DEFTYPE_UNION
        || node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
      switch (node_ident(m)) {
      case ID_OPERATOR_EQ:
        {
          G0(xiftag, body, IF,
             G(cmp, BIN,
               cmp->as.BIN.operator = TNE;
               G(accself, BIN,
                 accself->as.BIN.operator = TDOT;
                 G_IDENT(s, "self");
                 G(stag, IDENT,
                   stag->as.IDENT.name = ID_TAG));
               G(accother, BIN,
                 accother->as.BIN.operator = TDOT;
                 G_IDENT(o, "other");
                 G(otag, IDENT,
                   otag->as.IDENT.name = ID_TAG)));
             G(yestag, BLOCK,
               G(ret, RETURN,
                 G(fal, BOOL,
                   fal->as.BOOL.value = false)));
             G(notag, BLOCK,
               G(noop, NOOP)));
          need_true = true;
        }
        break;
      }

      if (node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
        G0(ret, body, RETURN,
           G(tr, BOOL,
             tr->as.BOOL.value = true));
        return;
      }

      if (node->as.DEFTYPE.kind == DEFTYPE_UNION) {
        G0(match, body, MATCH,
           G(self, IDENT,
             self->as.IDENT.name = arity >= 3 ? ID_OTHER : ID_SELF));
        body = match;
      }
    } else {
      switch (node_ident(m)) {
      case ID_OPERATOR_EQ:
        need_true = true;
        break;
      }
    }
  } else if (node->which == DEFCHOICE) {
    G0(pattern, body, IDENT,
       pattern->as.IDENT.name = node_ident(node));
    G0(block, body, BLOCK,
       G(noop, NOOP));
    body = block;

    const struct node *ext = node_defchoice_external_payload(node);
    if (ext != NULL) {
      gen_on_field(mod, m, body, node, ext->typ);
      return;
    } else if (subs_count(node) > IDX_CH_FIRST_PAYLOAD) {
      assert(false && "FIXME(e): Unsupported");
    }
  }

  bool has_defchoices = false;
  FOREACH_SUB(f, node) {
    if (NM(f->which) & NM(DEFCHOICE)) {
      gen_on_choices_and_fields(mod, f, m, body);
      has_defchoices = true;
    }
  }
  if (has_defchoices) {
    G0(oth, body, IDENT,
       oth->as.IDENT.name = ID_OTHERWISE);
    G0(noopb, body, BLOCK,
       G(noop, NOOP));
    body = parent(body);
  }

  FOREACH_SUB(f, node) {
    if (NM(f->which) & NM(DEFFIELD)) {
      gen_on_choices_and_fields(mod, f, m, body);
    }
  }

  if (need_true) {
    G0(bret, body, BLOCK,
       G(ret, RETURN,
         G(tr, BOOL,
           tr->as.BOOL.value = true)));
  }

  // For unions, the tag copy is inserted by cprinter.
}

static void gen_on_choices_and_fields_lexicographic(struct module *mod,
                                                    struct node *deft,
                                                    struct node *ch,
                                                    struct node *m,
                                                    struct node *body) {
  assert(deft->which == DEFTYPE);
  if (deft->as.DEFTYPE.kind == DEFTYPE_UNION) {
    // FIXME: unsupported
    return;
  } else {
    assert(ch == NULL);
  }

  const struct node *funargs = subs_at_const(m, IDX_FUNARGS);
  GSTART();
  struct node *par = body;
  size_t n = 0;
  FOREACH_SUB_CONST(f, deft) {
    if (f->which != DEFFIELD) {
      continue;
    }

    const struct node *arg_self = subs_first_const(funargs);
    const ident g = gensym(mod);
    G0(let, par, LET,
       G(defp, DEFPATTERN,
         G(var, IDENT,
           var->as.IDENT.name = g);
         G(call, CALL,
           G(fun, BIN,
             fun->as.BIN.operator = TDOT;
             like_arg(mod, fun, arg_self, f);
             G(name, IDENT,
               name->as.IDENT.name = node_ident(m))))));
    if (subs_count_atleast(funargs, 3)) {
      const struct node *arg_other = next_const(arg_self);
      like_arg(mod, call, arg_other, f);
    }

    G0(cond, par, IF,
       G(test, BIN,
         test->as.BIN.operator = TNE;
         G(var2, IDENT,
           var2->as.IDENT.name = g);
         G(zero, NUMBER,
           zero->as.NUMBER.value = "0"));
       G(yes, BLOCK,
         G(ret, RETURN,
           G(var3, IDENT,
             var3->as.IDENT.name = g)));
       G(no, BLOCK));

    par = no;
    n += 1;
  }

  G0(ret, par, RETURN,
     G(zero, NUMBER,
       zero->as.NUMBER.value = "0"));
}

static void gen_by_compare(struct module *mod, struct node *deft,
                           struct node *m, struct node *body) {
  enum token_type op = 0;
  switch (node_ident(m)) {
  case ID_OPERATOR_LE: op = TLE; break;
  case ID_OPERATOR_LT: op = TLT; break;
  case ID_OPERATOR_GT: op = TGT; break;
  case ID_OPERATOR_GE: op = TGE; break;
  default: assert(false);
  }

  GSTART();
  G0(ret, body, RETURN,
     G(test, BIN,
       test->as.BIN.operator = op;
       G(zero, NUMBER,
         zero->as.NUMBER.value = "0");
       G(call, CALL,
         G(fun, BIN,
           fun->as.BIN.operator = TDOT;
           G_IDENT(self, "self");
           G_IDENT(name, "Operator_compare"));
         G_IDENT(other, "other"))));
}

static void gen_enum_show_choices(struct module *mod, struct node *deft,
                             struct node *par, const struct node *node) {
  assert(NM(node->which) & (NM(DEFCHOICE) | NM(DEFTYPE)));

  if (node->which == DEFCHOICE && node->as.DEFCHOICE.is_leaf) {
    const char *n = idents_value(mod->gctx, node_ident(node));
    const size_t len = strlen(n);
    char *v = calloc(len + 3, sizeof(char));
    v[0] = '"';
    strcpy(v+1, n);
    v[len+1] = '"';

    GSTART();
    G0(pat, par, IDENT,
       pat->as.IDENT.name = node_ident(node));
    G0(bl, par, BLOCK,
       G(ignlet, LET,
         G(igndefp, DEFPATTERN,
           G(ign, IDENT,
             ign->as.IDENT.name = ID_OTHERWISE);
           G(append, CALL,
             G(fun, BIN,
               fun->as.BIN.operator = TSHARP;
               G_IDENT(st, "st");
               G_IDENT(app, "Write"));
             G(b, BIN,
               b->as.BIN.operator = TDOT;
               G(s, STRING,
                 s->as.STRING.value = v);
               G_IDENT(bytes, "Bytes"))))));
    return;
  }

  FOREACH_SUB_CONST(ch, node) {
    if (ch->which != DEFCHOICE) {
      continue;
    }

    gen_enum_show_choices(mod, deft, par, ch);
  }
}

static void gen_enum_show(struct module *mod, struct node *deft,
                     struct node *m, struct node *body) {
  GSTART();
  G0(match, body, MATCH,
     G_IDENT(self, "self"));

  gen_enum_show_choices(mod, deft, match, deft);
}

static void add_auto_member(struct module *mod,
                            struct node *deft,
                            const struct typ *inferred_intf,
                            const struct typ *intf,
                            const struct tit *mi) {
  if (typ_is_reference(deft->typ)) {
    return;
  }

  struct node *existing = node_get_member(deft, tit_ident(mi));
  if (existing != NULL) {
    return;
  }

  struct node *m = define_builtin_start(mod, deft, intf, mi);

  if (deft->which == DEFINCOMPLETE) {
    assert(deft->as.DEFINCOMPLETE.is_isalist_literal);
    define_builtin_catchup(mod, m);
    return;
  }

  if (typ_is_trivial(inferred_intf)
      || typ_equal(inferred_intf, TBI_ENUM)
      || typ_equal(inferred_intf, TBI_UNION)
      || typ_equal(inferred_intf, TBI_UNION_FROM_TAG_CTOR)) {
    enum builtingen bg = BG__NOT;

    switch (tit_ident(mi)) {
    case ID_CTOR:
      bg = BG_TRIVIAL_CTOR_CTOR;
      break;
    case ID_DTOR:
      bg = BG_TRIVIAL_DTOR_DTOR;
      break;
    case ID_COPY_CTOR:
      bg = BG_TRIVIAL_COPY_COPY_CTOR;
      break;
    case ID_OPERATOR_COMPARE:
      bg = BG_TRIVIAL_COMPARE_OPERATOR_COMPARE;
      break;
    case ID_SIZEOF:
      bg = BG_SIZEOF;
      break;
    case ID_ALIGNOF:
      bg = BG_ALIGNOF;
      break;
    case ID_MOVE:
      bg = BG_MOVE;
      break;
    case ID_FROM_TAG:
      bg = BG_ENUM_FROM_TAG;
      break;
    case ID_TAG:
      bg = BG_ENUM_TAG;
      break;
    default:
      goto non_bg;
    }

    node_toplevel(m)->builtingen = bg;
    define_builtin_catchup(mod, m);
    return;
  }

non_bg:;
  GSTART();
  G0(body, m, BLOCK);

  switch (tit_ident(mi)) {
  case ID_CTOR:
  case ID_DTOR:
  case ID_MOVE:
  case ID_COPY_CTOR:
  case ID_OPERATOR_EQ:
    gen_on_choices_and_fields(mod, deft, m, body);
    break;
  case ID_OPERATOR_NE:
    {
      GSTART();
      G0(ret, body, RETURN,
         G(not, UN,
           not->as.UN.operator = Tnot;
           G(call, CALL,
             G(f, BIN,
               f->as.BIN.operator = TDOT;
               G_IDENT(s, "self");
               G_IDENT(m, "Operator_eq"));
             G_IDENT(o, "other"))));
    }
    break;
  case ID_OPERATOR_COMPARE:
    gen_on_choices_and_fields_lexicographic(mod, deft, NULL, m, body);
    break;
  case ID_OPERATOR_LE:
  case ID_OPERATOR_LT:
  case ID_OPERATOR_GT:
  case ID_OPERATOR_GE:
    gen_by_compare(mod, deft, m, body);
    break;
  case ID_SHOW:
    gen_enum_show(mod, deft, m, body);
    break;
  default:
    assert(false);
  }
  define_builtin_catchup(mod, m);
}

static bool need_auto_member(struct typ *intf, struct tit *mi) {
  switch (tit_ident(mi)) {
  case ID_CTOR:
  case ID_DTOR:
  case ID_COPY_CTOR:
  case ID_SIZEOF:
  case ID_ALIGNOF:
  case ID_MOVE:
  case ID_OPERATOR_COMPARE:
  case ID_OPERATOR_EQ:
  case ID_OPERATOR_NE:
  case ID_OPERATOR_LE:
  case ID_OPERATOR_LT:
  case ID_OPERATOR_GT:
  case ID_OPERATOR_GE:
  case ID_FROM_TAG:
  case ID_TAG:
  case ID_SHOW:
    return true;
  default:
    return false;
  }
}

STEP_NM(step_autointf_newtype,
        NM(DEFTYPE));
error step_autointf_newtype(struct module *mod, struct node *node,
                            void *user, bool *stop) {
  struct node *nt = node->as.DEFTYPE.newtype_expr;
  if (nt == NULL) {
    return 0;
  }

  struct typ *t = nt->typ;
  if (typ_definition_which(t) != DEFTYPE) {
    error e = mk_except_type(mod, node, "newtype argument must be a"
                             " struct, enum, or union, not '%s'", pptyp(mod, t));
    THROW(e);
  }

  struct tit *ms = typ_definition_members(t, DEFFUN, DEFMETHOD, LET, 0);
  while (tit_next(ms)) {
    ident name = tit_ident(ms);;
    if (tit_which(ms) == LET) {
      struct tit *def = tit_let_def(ms);
      name = tit_ident(def);
      tit_next(def);
    }

    if (name == ID_THIS || name == ID_FINAL) {
      continue;
    }

    struct node *existing = node_get_member(node, name);
    if (existing != NULL) {
      continue;
    }

    struct node *m = define_builtin_start(mod, node, node->typ, ms);
    if (m->which == DEFFUN) {
      m->as.DEFFUN.is_newtype_pretend_wrapper = true;
    } else if (m->which == DEFMETHOD) {
      m->as.DEFMETHOD.is_newtype_pretend_wrapper = true;
    }
    define_builtin_catchup(mod, m);
  }

  const ident namet = typ_definition_ident(t);
  const char *namet_s = idents_value(mod->gctx, namet);

  char *n = calloc(sizeof("From_") + strlen(namet_s), 1);
  sprintf(n, "%crom_%s", "fF"[!!isupper(namet_s[0])], namet_s);
  n[sizeof("From_")-1] = tolower(n[sizeof("From_")-1]);
  const ident from_name = idents_add_string(mod->gctx, n, strlen(n));
  free(n);
  const ident to_name = namet;

  GSTART();
  if (node_get_member(node, from_name) == NULL) {
    G0(from, node, DEFFUN,
       from->as.DEFFUN.is_newtype_converter = true;
       G(id, IDENT,
         id->as.IDENT.name = from_name);
       G(ga, GENARGS);
       G(args, FUNARGS,
         G(arg, DEFARG,
           G_IDENT(narg, "x");
           G(targ, DIRECTDEF,
             set_typ(&targ->as.DIRECTDEF.typ, t)));
         G(ret, DEFARG,
           G(nret, IDENT,
             nret->as.IDENT.name = ID_NRETVAL);
           G(tret, IDENT,
             tret->as.IDENT.name = ID_THIS);
           ret->as.DEFARG.is_retval = true));
       G(w, WITHIN));
    deffun_count_args(from);
    define_builtin_catchup(mod, from);
  }

  if (node_get_member(node, to_name) == NULL) {
    G0(to, node, DEFMETHOD,
       to->as.DEFMETHOD.is_newtype_converter = true;
       G(id, IDENT,
         id->as.IDENT.name = to_name);
       G(ga, GENARGS);
       G(args, FUNARGS,
         G(self, DEFARG,
           G(nself, IDENT,
             nself->as.IDENT.name = ID_SELF);
           G(rself, UN,
             rself->as.UN.operator = TREFDOT;
             G(tself, IDENT,
               tself->as.IDENT.name = ID_THIS)));
         G(ret, DEFARG,
           G(nret, IDENT,
             nret->as.IDENT.name = ID_NRETVAL);
           G(tret, DIRECTDEF,
             set_typ(&tret->as.DIRECTDEF.typ, t));
           ret->as.DEFARG.is_retval = true));
       G(w, WITHIN));
    deffun_count_args(to);
    define_builtin_catchup(mod, to);
  }

  return 0;
}

static ERROR add_auto_isa_eachisalist(struct module *mod,
                                      struct typ *inferred_intf,
                                      struct typ *intf,
                                      bool *stop,
                                      void *user) {
  struct node *deft = user;
  struct tit *mi = typ_definition_members(intf, DEFMETHOD, DEFFUN, 0);
  while (tit_next(mi)) {
    if (need_auto_member(intf, mi)) {
      add_auto_member(mod, deft, inferred_intf, intf, mi);
    }
  }

  return 0;
}

static void add_auto_isa_isalist(struct module *mod, struct node *deft,
                                 const struct typ *i) {
  if (!typ_isa(deft->typ, i)) {
    struct node *isalist = subs_at(deft, IDX_ISALIST);
    assert(isalist->which == ISALIST);

    GSTART();
    G0(isa, isalist, ISA,
       isa->as.ISA.is_export = node_is_export(deft)
       && (node_is_inline(deft) || node_is_opaque(deft));
       G(what, DIRECTDEF);
       set_typ(&what->as.DIRECTDEF.typ, CONST_CAST(i)));

    error e = catchup(mod, NULL, isa, CATCHUP_BELOW_CURRENT);
    assert(!e);

    typ_create_update_quickisa(deft->typ);
  }
}

static void add_auto_isa(struct module *mod, struct node *deft,
                         const struct typ *i) {
  add_auto_isa_isalist(mod, deft, i);

  if (deft->which == DEFINTF) {
    return;
  }

  if (!(node_is_extern(deft) && !typ_is_trivial(i))) {
    error never = add_auto_isa_eachisalist(mod, CONST_CAST(i),
                                           CONST_CAST(i),
                                           NULL, deft);
    assert(!never);
  }

  const bool non_trivial_on_extern = !typ_is_trivial(i) && node_is_extern(deft);

  const uint32_t filter = non_trivial_on_extern
                          ? ISALIST_FILTEROUT_NONTRIVIAL_ISALIST
                          : 0;
  error never = typ_isalist_foreach(mod, CONST_CAST(i), filter,
                                    add_auto_isa_eachisalist, deft);
  assert(!never);
}

static size_t count_defchoices(const struct node *node) {
  size_t count = 0;
  FOREACH_SUB_CONST(d, node) {
    if (d->which == DEFCHOICE) {
      if (d->as.DEFCHOICE.is_leaf) {
        count += 1;
      } else {
        count += count_defchoices(d);
      }
    }
  }
  return count;
}

static void add_enum_constants(struct module *mod, struct node *node) {
  const size_t count = count_defchoices(node);

  {
    char value[32] = { 0 };
    snprintf(value, ARRAY_SIZE(value), "0x%"PRIx64, count);

    GSTART();
    G0(let, node, LET,
       let->flags |= NODE_IS_GLOBAL_LET;
       G(defn, DEFNAME,
         G_IDENT(valn, "COUNT");
         G(valc, TYPECONSTRAINT,
           G(val, NUMBER,
             val->as.NUMBER.value = strdup(value));
           G(valt, DIRECTDEF,
             set_typ(&valt->as.DIRECTDEF.typ, TBI_UINT)))));

    error e = catchup(mod, NULL, let, CATCHUP_BELOW_CURRENT);
    assert(!e);
  }

  if (count > 64) {
    // FIXME
    return;
  }

  {
    const uint64_t bwall = (1 << count) - 1;

    char value[32] = { 0 };
    snprintf(value, ARRAY_SIZE(value), "0x%"PRIx64, bwall);

    GSTART();
    G0(let, node, LET,
       let->flags |= NODE_IS_GLOBAL_LET;
       G(defn, DEFNAME,
         G_IDENT(valn, "BWALL");
         G(valc, TYPECONSTRAINT,
           G(val, NUMBER,
             val->as.NUMBER.value = strdup(value));
           G(valt, DIRECTDEF,
             set_typ(&valt->as.DIRECTDEF.typ, TBI_U64)))));

    error e = catchup(mod, NULL, let, CATCHUP_BELOW_CURRENT);
    assert(!e);
  }
}

STEP_NM(step_autointf_enum_union_isalist,
        NM(DEFTYPE));
error step_autointf_enum_union_isalist(struct module *mod, struct node *node,
                                       void *user, bool *stop) {
  DSTEP(mod, node);
  if (node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
    add_enum_constants(mod, node);
    add_auto_isa_isalist(mod, node, TBI_ENUM);
  } else if (node->as.DEFTYPE.kind == DEFTYPE_UNION) {
    add_auto_isa_isalist(mod, node, TBI_UNION);
  }

  return 0;
}

STEP_NM(step_autointf_enum_union,
        NM(DEFTYPE));
error step_autointf_enum_union(struct module *mod, struct node *node,
                               void *user, bool *stop) {
  DSTEP(mod, node);
  if (node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
    add_auto_isa(mod, node, TBI_ENUM);

    if (node->as.DEFTYPE.default_choice != NULL) {
      add_auto_isa(mod, node, TBI_TRIVIAL_CTOR);
    }
  } else if (node->as.DEFTYPE.kind == DEFTYPE_UNION) {
    add_auto_isa(mod, node, TBI_UNION);
  }

  return 0;
}

STEP_NM(step_autointf_detect_default_ctor_dtor,
        NM(DEFTYPE));
error step_autointf_detect_default_ctor_dtor(struct module *mod, struct node *node,
                                             void *user, bool *stop) {
  DSTEP(mod, node);

  if (node_is_extern(node)) {
    return 0;
  }
  if (node->as.DEFTYPE.kind == DEFTYPE_ENUM
      || node->as.DEFTYPE.kind == DEFTYPE_UNION) {
    return 0;
  }

  struct node *proxy = node;
  struct node *ctor = node_get_member(proxy, ID_CTOR);
  if (ctor != NULL) {
    if (node_fun_max_args_count(ctor) == 0) {
      add_auto_isa(mod, node, TBI_DEFAULT_CTOR);
    }
  } else {
    // see step_autointf_infer_intfs
  }

  return 0;
}

struct inferred {
  bool default_ctor, default_dtor, copyable, return_by_copy, moveable,
       equality_by_compare, ordered_by_compare;
  bool trivial_ctor, trivial_dtor, trivial_copy_but_owned, trivial_move,
       trivial_copy, trivial_compare, trivial_equality, trivial_order;
  bool has_equality;
};

static void infer(struct inferred *inferred, const struct node *node) {
  if (node->which == DEFCHOICE) {
    const struct node *ext = node_defchoice_external_payload(node);
    if (ext == NULL) {
      FOREACH_SUB_CONST(f, node) {
        if (!(NM(f->which) & (NM(DEFFIELD) | NM(DEFCHOICE)))) {
          continue;
        }

        infer(inferred, f);
      }
      return;
    }

    node = ext;
  }

  if (inferred->default_ctor) {
    inferred->trivial_ctor &= inferred->default_ctor &= typ_isa(node->typ, TBI_DEFAULT_CTOR);
  }
  if (inferred->default_dtor) {
    inferred->trivial_copy_but_owned &= inferred->trivial_copy &=
      inferred->trivial_dtor &= inferred->default_dtor &= typ_isa(node->typ, TBI_DEFAULT_DTOR);
  }
  if (inferred->copyable) {
    inferred->trivial_copy &= inferred->copyable &= typ_isa(node->typ, TBI_COPYABLE);
  }
  if (inferred->moveable) {
    inferred->trivial_move &= inferred->moveable &= typ_isa(node->typ, TBI_MOVEABLE);
  }
  if (inferred->has_equality) {
    inferred->trivial_equality &= inferred->has_equality &= typ_isa(node->typ, TBI_HAS_EQUALITY);
  }
  if (inferred->equality_by_compare) {
    inferred->equality_by_compare &= typ_isa(node->typ, TBI_EQUALITY_BY_COMPARE);
  }
  if (inferred->ordered_by_compare) {
    inferred->ordered_by_compare &= typ_isa(node->typ, TBI_ORDERED_BY_COMPARE);
  }
  if (inferred->return_by_copy) {
    inferred->return_by_copy &= typ_isa(node->typ, TBI_RETURN_BY_COPY);
  }

  if (inferred->trivial_ctor) {
    inferred->trivial_ctor &= typ_isa(node->typ, TBI_TRIVIAL_CTOR);
  }
  if (inferred->trivial_dtor) {
    inferred->trivial_copy_but_owned &= inferred->trivial_copy &=
      inferred->trivial_dtor &= typ_isa(node->typ, TBI_TRIVIAL_DTOR);
  }
  if (inferred->trivial_copy_but_owned) {
    inferred->trivial_copy_but_owned &= typ_isa(node->typ, TBI_TRIVIAL_COPY_BUT_OWNED);
  }
  if (inferred->trivial_copy) {
    inferred->trivial_copy &= typ_isa(node->typ, TBI_TRIVIAL_COPY);
  }
  if (inferred->trivial_move) {
    inferred->trivial_move &= typ_isa(node->typ, TBI_TRIVIAL_MOVE);
  }
  if (inferred->trivial_compare) {
    inferred->trivial_order &= inferred->trivial_equality &= inferred->trivial_compare &=
      typ_isa(node->typ, TBI_TRIVIAL_COMPARE);
  }
  if (inferred->trivial_equality) {
    inferred->trivial_equality &= typ_isa(node->typ, TBI_TRIVIAL_EQUALITY);
  }
  if (inferred->trivial_order) {
    inferred->trivial_order &= typ_isa(node->typ, TBI_TRIVIAL_ORDER);
  }
}

static void infer_top(struct inferred *inferred, const struct node *node) {
  inferred->trivial_ctor =
    NULL == node_get_member_const(node, ID_CTOR);
  inferred->trivial_copy = inferred->trivial_copy_but_owned =
    NULL == node_get_member_const(node, ID_COPY_CTOR);
  inferred->trivial_copy &= inferred->trivial_copy_but_owned &=
    inferred->trivial_dtor =
    NULL == node_get_member_const(node, ID_DTOR);
  inferred->trivial_move =
    NULL == node_get_member_const(node, ID_MOVE);
  inferred->trivial_order = inferred->trivial_equality = inferred->trivial_compare =
    NULL == node_get_member_const(node, ID_OPERATOR_LE)
    && NULL == node_get_member_const(node, ID_OPERATOR_LT)
    && NULL == node_get_member_const(node, ID_OPERATOR_GT)
    && NULL == node_get_member_const(node, ID_OPERATOR_GE)
    && NULL == node_get_member_const(node, ID_OPERATOR_COMPARE);
  inferred->trivial_equality =
    NULL == node_get_member_const(node, ID_OPERATOR_EQ)
    && NULL == node_get_member_const(node, ID_OPERATOR_NE);

  FOREACH_SUB_CONST(f, node) {
    if (!(NM(f->which) & (NM(DEFFIELD) | NM(DEFCHOICE)))) {
      continue;
    }

    infer(inferred, f);
  }
}

static bool immediate_isa(const struct node *node, const struct typ *i) {
  const struct node *isalist = subs_at_const(node, IDX_ISALIST);
  FOREACH_SUB_CONST(isa, isalist) {
    if (typ_equal(isa->typ, i)) {
      return true;
    }
  }
  return false;
}

STEP_NM(step_autointf_infer_intfs,
        NM(DEFTYPE));
error step_autointf_infer_intfs(struct module *mod, struct node *node,
                                void *user, bool *stop) {
  DSTEP(mod, node);

  if (typ_equal(node->typ, TBI_VOID)) {
    return 0;
  }

  add_auto_isa(mod, node, TBI_ANY);

  if (node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
    return 0;
  }

  struct inferred inferred = {
    true, true, true, true, true, true,
    true, true, true, true, true, true, true, true,
    true, true,
  };

  if (node_is_extern(node)) {
    inferred = (struct inferred){ 0 };
    goto skip;
  }

  // Any isa at this point has been explicitly declared by the user.
  if (immediate_isa(node, TBI_NON_DEFAULT_CTOR)) {
    inferred.default_ctor = inferred.trivial_ctor = false;
  } else if (immediate_isa(node, TBI_DEFAULT_CTOR)) {
    inferred.trivial_ctor = false;
  }
  if (immediate_isa(node, TBI_DEFAULT_DTOR)) {
    inferred.trivial_dtor = false;
  } else if (immediate_isa(node, TBI_ERROR_DTOR)) {
    inferred.trivial_dtor = inferred.default_dtor = false;
  }
  if (immediate_isa(node, TBI_NOT_COPYABLE)) {
    inferred.return_by_copy = inferred.trivial_copy
      = inferred.trivial_copy_but_owned = inferred.copyable = false;
  } else if (immediate_isa(node, TBI_COPYABLE)) {
    inferred.trivial_copy = false;
  }
  if (immediate_isa(node, TBI_NOT_MOVEABLE)) {
    inferred.trivial_move = inferred.moveable = false;
  } else if (immediate_isa(node, TBI_MOVEABLE)) {
    inferred.trivial_move = false;
  }
  if (immediate_isa(node, TBI_NOT_HAS_EQUALITY)) {
    inferred.equality_by_compare = inferred.trivial_equality = inferred.has_equality = false;
  } else if (immediate_isa(node, TBI_HAS_EQUALITY)
             || immediate_isa(node, TBI_EQUALITY_BY_COMPARE)) {
    inferred.trivial_equality = false;
  }
  if (immediate_isa(node, TBI_NOT_ORDERED)) {
    inferred.ordered_by_compare = inferred.trivial_order = false;
  } else if (immediate_isa(node, TBI_ORDERED)
             || immediate_isa(node, TBI_ORDERED_BY_COMPARE)) {
    inferred.trivial_order = false;
  }
  if (immediate_isa(node, TBI_NOT_RETURN_BY_COPY)) {
    inferred.return_by_copy = false;
  }

  infer_top(&inferred, node);

skip:

  if (node->as.DEFTYPE.kind != DEFTYPE_UNION) {
    if (inferred.trivial_ctor || typ_isa(node->typ, TBI_TRIVIAL_CTOR)) {
      add_auto_isa(mod, node, TBI_TRIVIAL_CTOR);
    } else if (inferred.default_ctor || typ_isa(node->typ, TBI_DEFAULT_CTOR)) {
      add_auto_isa(mod, node, TBI_DEFAULT_CTOR);
    }
  } else {
    const struct node *default_choice = node->as.DEFTYPE.default_choice;
    // infer() treats all DEFCHOICE symmetrically, but for ctor interfaces
    // on a union, we actually only need to consider default_choice, if
    // there is one; except for `Union_from_tag_ctor where are fields are of
    // equal importance.
    if (inferred.trivial_ctor || typ_isa(node->typ, TBI_TRIVIAL_CTOR)) {
      // All fields are `Trivial_ctor.
      add_auto_isa(mod, node, TBI_UNION_FROM_TAG_CTOR);
    } else if (default_choice != NULL && typ_isa(default_choice->typ, TBI_TRIVIAL_CTOR)) {
      add_auto_isa(mod, node, TBI_TRIVIAL_CTOR);
    } else if (inferred.default_ctor || typ_isa(node->typ, TBI_DEFAULT_CTOR)
               || (default_choice != NULL && typ_isa(default_choice->typ, TBI_TRIVIAL_CTOR))) {
      add_auto_isa(mod, node, TBI_DEFAULT_CTOR);
    }
  }

  if (inferred.trivial_dtor || typ_isa(node->typ, TBI_TRIVIAL_DTOR)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_DTOR);
  } else if (inferred.default_dtor || typ_isa(node->typ, TBI_DEFAULT_DTOR)) {
    add_auto_isa(mod, node, TBI_DEFAULT_DTOR);
  }
  if (inferred.trivial_copy_but_owned || typ_isa(node->typ, TBI_TRIVIAL_COPY_BUT_OWNED)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_COPY_BUT_OWNED);
  } else if (inferred.trivial_copy || typ_isa(node->typ, TBI_TRIVIAL_COPY)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_COPY);
  } else {
    // Only infer `return_by_copy if `trivial_copy.
    inferred.return_by_copy = false;

    if (inferred.copyable) {
      add_auto_isa(mod, node, TBI_COPYABLE);
    }
  }
  if (inferred.trivial_move || typ_isa(node->typ, TBI_TRIVIAL_MOVE)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_MOVE);
  } else if (inferred.moveable) {
    add_auto_isa(mod, node, TBI_MOVEABLE);
  }
  if (inferred.trivial_compare || typ_isa(node->typ, TBI_TRIVIAL_COMPARE)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_COMPARE);
  }
  if (inferred.trivial_order || typ_isa(node->typ, TBI_TRIVIAL_ORDER)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_ORDER);
  } else if (inferred.ordered_by_compare || typ_isa(node->typ, TBI_ORDERED_BY_COMPARE)) {
    add_auto_isa(mod, node, TBI_ORDERED_BY_COMPARE);
  } else if (inferred.trivial_equality || typ_isa(node->typ, TBI_TRIVIAL_EQUALITY)) {
    add_auto_isa(mod, node, TBI_TRIVIAL_EQUALITY);
  } else if (inferred.equality_by_compare) {
    add_auto_isa(mod, node, TBI_EQUALITY_BY_COMPARE);
  } else if (inferred.has_equality) {
    add_auto_isa(mod, node, TBI_HAS_EQUALITY);
  }
  if (inferred.return_by_copy) {
    add_auto_isa(mod, node, TBI_RETURN_BY_COPY);
  }

  return 0;
}

STEP_NM(step_autointf_isalist_literal_protos,
        NM(DEFINCOMPLETE));
error step_autointf_isalist_literal_protos(struct module *mod, struct node *node,
                                           void *user, bool *stop) {
  DSTEP(mod, node);
  if (!node->as.DEFINCOMPLETE.is_isalist_literal) {
    return 0;
  }

  const struct node *isalist = subs_at_const(node, IDX_ISALIST);
  FOREACH_SUB_CONST(isa, isalist) {
    add_auto_isa(mod, node, isa->typ);
  }

  return 0;
}

STEP_NM(step_autointf_inherit,
        NM(DEFTYPE) | NM(DEFINTF));
error step_autointf_inherit(struct module *mod, struct node *node,
                            void *user, bool *stop) {
  DSTEP(mod, node);

  struct node *isalist = subs_at(node, IDX_ISALIST);
  FOREACH_SUB(isa, isalist) {
    struct typ *i0 = typ_generic_functor(isa->typ);
    if (i0 == NULL || !typ_equal(i0, TBI_INHERIT)) {
      continue;
    }

    struct typ *what = typ_generic_arg(isa->typ, 0);
    struct typ *t = typ_generic_arg(isa->typ, 1);
    if (typ_isa(t, what)) {
      add_auto_isa(mod, node, what);
    }
  }
  return 0;
}
