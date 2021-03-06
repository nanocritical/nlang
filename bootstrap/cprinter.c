#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include <stdio.h>

#include "printer.h"
#include "types.h"
#include "topdeps.h"
#include "scope.h"
#include "constraints.h"
#include "reflect.h"
#include "useorder.h"

#include <stdarg.h>
#include <ctype.h>

struct out {
  FILE *c;
  size_t c_line;
  bool last_was_semi;

  // Keep track of output byte count as clang is particularly difficult
  // about empty statements in the wrong place (e.g. the expression
  // ({ 0;; }) gets typed as void, while gcc is willing to see an int).
  size_t runcount;

  const struct module *mod;
  const struct node *node;
  size_t n_line;

  FILE *linemap;

  char buf[16384];
};

static void pf(struct out *out, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
static void pf(struct out *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int cnt = vsnprintf(out->buf, ARRAY_SIZE(out->buf), fmt, ap);
  va_end(ap);
  assert(cnt < ARRAY_SIZE(out->buf) && "buf too small");

  out->runcount += cnt;

  size_t c_line_incr = 0;
  for (char *p = out->buf; p != out->buf + cnt; ++p) {
    c_line_incr += !!(p[0] == '\n');
  }

  fprintf(out->c, "%s", out->buf);

  if (c_line_incr == 0) {
    return;
  }

  out->c_line += c_line_incr;

  if (out->node != NULL && out->node->codeloc.line != out->n_line) {
    out->n_line = out->node->codeloc.line;

    fprintf(out->linemap, "%zd %d %s\n", out->c_line, out->node->codeloc.line,
            module_component_filename_at(out->mod, out->node->codeloc.pos));
  }
}

#define UNUSED "__attribute__((__unused__))"
#define WEAK "__attribute__((__weak__))"
#define ALWAYS_INLINE
// "__attribute__((__always_inline__))\n"
#define SECTION_EXAMPLES "__attribute__((section(\".text.n.examples\")))"

#define DEF(t) typ_definition_ignore_any_overlay_const(t)

static bool skip(bool header, enum forward fwd, const struct node *node) {
  return false;
  const bool function = node_is_fun(node);
  const bool let = node->which == LET;

  const bool gen = typ_generic_arity(node->typ) > 0
    || ((function || let) && typ_generic_arity(parent_const(node)->typ) > 0);
  const bool hinline = node_is_inline(node);
  const bool hvisible = node_is_export(node) || hinline;

  const enum forward decl = (function || let) ? FWD_DECLARE_FUNCTIONS : FWD_DECLARE_TYPES;
  const enum forward def = (function || let) ? FWD_DEFINE_FUNCTIONS : FWD_DEFINE_TYPES;
  const enum forward dyn = (function || let) ? FORWARD__NUM /* means never */ : FWD_DEFINE_DYNS;

  if (gen && node->which != DEFINTF) {
    // Delegate decision to print_topdeps().
    return false;
  }

  if (fwd == decl) {
    return (!header && hvisible) || (header && !hvisible);
  } else if (fwd == def) {
    return (!header && hinline) || (header && !hinline);
  } else if (fwd == dyn) {
    return (!header && hinline) || (header && !hvisible);
  } else if ((NM(node->which) & (NM(DEFTYPE) | NM(DEFINTF)))
             && (fwd == FWD_DECLARE_FUNCTIONS || fwd == FWD_DEFINE_FUNCTIONS)) {
    return header && !hvisible;
  } else {
    return true;
  }
}

static const char *c_token_strings[TOKEN__NUM] = {
  [TASSIGN] = " = ",
  [TEQ] = " == ",
  [TNE] = " != ",
  [TEQPTR] = " == ",
  [TNEPTR] = " != ",
  [TLE] = " <= ",
  [TLT] = " < ",
  [TGT] = " > ",
  [TGE] = " >= ",
  [TPLUS] = " + ",
  [TMINUS] = " - ",
  [TUPLUS] = "+",
  [TUMINUS] = "-",
  [TTIMES] = " * ",
  [TDIVIDE] = " / ",
  [TMODULO] = " % ",
  [TOVPLUS] = " + ",
  [TOVMINUS] = " - ",
  [TOVUPLUS] = "+",
  [TOVUMINUS] = "-",
  [TOVTIMES] = " * ",
  [TOVDIVIDE] = " / ",
  [TOVMODULO] = " % ",
  [TBWAND] = " & ",
  [TBWOR] = " | ",
  [TBWXOR] = " ^ ",
  [TRSHIFT] = " >> ",
  [TOVLSHIFT] = " << ",
  [Tor] = " || ",
  [Tand] = " && ",
  [Tnot] = "!",
  [Tfalse] = "false",
  [Ttrue] = "true",
  [TPLUS_ASSIGN] = " += ",
  [TMINUS_ASSIGN] = " -= ",
  [TTIMES_ASSIGN] = " *= ",
  [TDIVIDE_ASSIGN] = " /= ",
  [TMODULO_ASSIGN] = " %= ",
  [TOVPLUS_ASSIGN] = " += ",
  [TOVMINUS_ASSIGN] = " -= ",
  [TOVTIMES_ASSIGN] = " *= ",
  [TOVDIVIDE_ASSIGN] = " /= ",
  [TOVMODULO_ASSIGN] = " %= ",
  [TBWAND_ASSIGN] = " &= ",
  [TBWOR_ASSIGN] = " |= ",
  [TBWXOR_ASSIGN] = " ^= ",
  [TRSHIFT_ASSIGN] = " >>= ",
  [TOVLSHIFT_ASSIGN] = " <<= ",
  [TBWNOT] = " ~",
  [TARROW] = " -> ",
  [TCOLON] = ":",
  [TCOMMA] = ", ",
  [TSEMICOLON] = "; ",
  [TRSBRA] = "]",
  [TLCBRA] = "{",
  [TRCBRA] = "}",
  [TLPAR] = "(",
  [TRPAR] = ")",
};

static char *escape_string(const char *s) {
  const size_t len = strlen(s);
  char *r = calloc(2 * len + 1, sizeof(char));
  if (len == 0) {
    return r;
  }

  char delim = s[0];
  if ((delim != '\'' && delim != '"') || len == 1) {
    delim = 0;
  }

  if (delim != 0 && s[2] == '\0' && s[1] == delim) {
    return r;
  }

  char *v = r;
  int off = delim != 0 ? 1 : 0;
  for (const char *p = s + off; p[off] != '\0'; ++p, ++v) {
    if (delim != 0 && p[1] == '\0' && p[0] == delim) {
      v[0] = '\0';
    } else if (p[0] == '"') {
      v[0] = '\\';
      v[1] = '"';
      v += 1;
    } else if (p[0] == '\\') {
      if (delim != 0 && p[1] == delim) {
        if (delim == '"') {
          v[0] = '\\';
          v[1] = '"';
          v += 1;
        } else {
          v[0] = delim;
        }
        p += 1;
      } else {
        v[0] = p[0];
      }
    } else {
      v[0] = p[0];
    }
  }

  return r;
}

EXAMPLE(escape_string) {
  assert(strcmp("abc", escape_string("\"abc\"")) == 0);
  assert(strcmp("ab'c", escape_string("\"ab'c\"")) == 0);
  assert(strcmp("abc", escape_string("'abc'")) == 0);
  assert(strcmp(" \\\"abc", escape_string("\" \"abc\"")) == 0);
  assert(strcmp("", escape_string("\"\"")) == 0);
  assert(strcmp("", escape_string("''")) == 0);
  assert(strcmp("'", escape_string("'\\''")) == 0);
  assert(strcmp("\\'", escape_string("\"\\'\"")) == 0);
  assert(strcmp("\\\"", escape_string("\"\\\"\"")) == 0);
  assert(strcmp("\n", escape_string("'\n'")) == 0);
  assert(strcmp("t00/automagicref.n:76:10",
                escape_string("\"t00/automagicref.n:76:10\"")) == 0);

  // Not quoted.
  assert(strcmp("abc", escape_string("abc")) == 0);
  assert(strcmp("ab'c", escape_string("ab'c")) == 0);
  assert(strcmp("abc", escape_string("'abc'")) == 0);
  assert(strcmp(" \\\"abc", escape_string(" \"abc")) == 0);
  assert(strcmp("", escape_string("")) == 0);
  assert(strcmp("", escape_string("''")) == 0);
  assert(strcmp("'", escape_string("'\\''")) == 0);
  assert(strcmp("\\'", escape_string("\\'")) == 0);
  assert(strcmp("\\\"", escape_string("\"")) == 0);
  assert(strcmp("\\\\\"", escape_string("\\\"")) == 0);
  assert(strcmp("\n", escape_string("'\n'")) == 0);
  assert(strcmp("t00/automagicref.n:76:10",
                escape_string("t00/automagicref.n:76:10")) == 0);
}

static void print_scope_last_name(struct out *out, const struct module *mod,
                                  const struct node *scoper) {
  const ident id = node_ident(scoper);
  const char *name = idents_value(mod->gctx, id);
  if (id == ID_ANONYMOUS) {
    return;
  }

  if (name[0] == '`') {
    pf(out, "_$Ni_%s", name + 1);
  } else {
    pf(out, "%s", name);
  }
}

static void print_scope_name(struct out *out, const struct module *mod,
                             const struct node *scoper) {
  if (parent_const(parent_const(scoper)) != NULL) {
    print_scope_name(out, mod, parent_const(scoper));
    if (node_ident(scoper) != ID_ANONYMOUS) {
      pf(out, "$");
    }
  }

  print_scope_last_name(out, mod, scoper);
}

static size_t c_fun_args_count(const struct node *node) {
  return node_fun_all_args_count(node);
}

static void print_token(struct out *out, enum token_type t) {
  pf(out, "%s", c_token_strings[t]);
}

static bool is_in_topmost_module(const struct node *node) {
  const struct module *mod = node_module_owner_const(node);
  return mod->stage->printing_mod == mod;
}

static bool is_in_topmost_module_typ(const struct typ *t) {
  return is_in_topmost_module(DEF(t));
}

static void bare_print_typ(struct out *out, const struct module *mod, const struct typ *typ);
static void bare_print_typ_actual(struct out *out, const struct module *mod, const struct typ *typ);
static void print_expr(struct out *out, const struct module *mod,
                       const struct node *node, uint32_t parent_op);
static void print_block(struct out *out, const struct module *mod,
                        const struct node *node);
static void print_typ(struct out *out, const struct module *mod, const struct typ *typ);
static void print_typeconstraint(struct out *out, const struct module *mod,
                                 const struct node *node);
static void print_ident(struct out *out, const struct module *mod,
                        const struct node *node);
static void print_defname(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node);
static void print_statement(struct out *out, const struct module *mod,
                            const struct node *node);
static void print_top(struct out *out, bool header, enum forward fwd,
                      const struct module *mod, const struct node *node,
                      struct fintypset *printed, bool force);
static void print_defchoice_path(struct out *out,
                                 const struct module *mod,
                                 const struct node *deft,
                                 const struct node *ch);
static void print_defintf(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node);

static bool ident_is_spurious_ssa_var(const struct node *node) {
  assert(node->which == IDENT);
  return typ_equal(node->typ, TBI_VOID)
    && node->as.IDENT.def->which == DEFNAME
    && node->as.IDENT.def->as.DEFNAME.ssa_user != NULL;
}

static void print_bin_sym_ptr_term(struct out *out, const struct module *mod,
                                   const struct node *term, uint32_t op) {

  if (typ_is_counted_reference(term->typ)) {
    pf(out, "(");
    print_typ(out, mod, typ_generic_arg_const(term->typ, 0));
    pf(out, "*)");
  }

  if (typ_is_dyn(term->typ) && term->which != NIL) {
    pf(out, "(");
    print_expr(out, mod, term, op);
    pf(out, ").ref");
    if (typ_is_counted_reference(term->typ)) {
      pf(out, ".ref");
    }
  } else if (typ_is_counted_reference(term->typ)) {
    pf(out, "(");
    print_expr(out, mod, term, op);
    pf(out, ").ref");
  } else {
    print_expr(out, mod, term, op);
  }
}

static void print_bin_sym(struct out *out, const struct module *mod, const struct node *node, uint32_t parent_op) {
  const uint32_t op = node->as.BIN.operator;
  const struct node *left = subs_first_const(node);
  const struct node *right = subs_last_const(node);
  if (op == TASSIGN && typ_equal(left->typ, TBI_VOID)) {
    assert(ident_is_spurious_ssa_var(left));

    if (right->which == IDENT) {
      assert(ident_is_spurious_ssa_var(right));
    } else {
      print_expr(out, mod, right, T__STATEMENT);
    }
  } else if (op == TASSIGN && right->which == INIT) {
    print_expr(out, mod, right, T__STATEMENT);
  } else if (op == TASSIGN && right->which == CALL
             && !typ_isa(right->typ, TBI_RETURN_BY_COPY)) {
    print_expr(out, mod, right, T__STATEMENT);
  } else if (op == TEQPTR || op == TNEPTR) {
    print_bin_sym_ptr_term(out, mod, left, op);
    print_token(out, op);
    print_bin_sym_ptr_term(out, mod, right, op);
  } else {
    const bool is_assign = OP_IS_ASSIGN(op);
    if (is_assign) {
      pf(out, "(void) ( ");
    }
    if (!is_assign || node_ident(left) != ID_OTHERWISE) {
      print_expr(out, mod, left, op);
      print_token(out, op);
    }
    print_expr(out, mod, right, op);
    if (is_assign) {
      pf(out, " )");
    }
  }
}

static void print_union_access_path(struct out *out, const struct module *mod,
                                    const struct typ *t, ident tag) {
  if (typ_is_reference(t)) {
    t = typ_generic_arg_const(t, 0);
  }

  const struct node *d = DEF(t);
  const struct node *defch = node_get_member_const(d, tag);

  struct node *field = NULL;
  while (true) {
    error e = scope_lookup_ident_immediate(&field, NULL, mod, defch,
                                           tag, true);
    if (!e) {
      break;
    }

    assert(defch->which != DEFTYPE);

    defch = parent_const(defch);
  }

  struct vecnode stack = { 0 };
  const struct node *p = field;
  while (p->which != DEFTYPE) {
    vecnode_push(&stack, CONST_CAST(p));
    p = parent_const(p);
  }

  for (size_t n = vecnode_count(&stack); n > 0; --n) {
    p = *vecnode_get(&stack, n - 1);
    pf(out, "as.%s", idents_value(mod->gctx, node_ident(p)));
  }
  vecnode_destroy(&stack);
}

static void print_union_init_access_path(struct out *out, const struct module *mod,
                                         const struct node *node) {
  assert(node->which == INIT);

  const ident tag = node->as.INIT.for_tag;
  if (tag == ID__NONE) {
    return;
  }

  print_union_access_path(out, mod, node->typ, tag);
  pf(out, ".");
}

static void print_bin_acc(struct out *out, const struct module *mod,
                          const struct node *node, uint32_t parent_op) {
  const uint32_t op = node->as.BIN.operator;
  const struct node *base = subs_first_const(node);
  const struct node *name = subs_last_const(node);
  const char *name_s = idents_value(mod->gctx, node_ident(name));

  const struct node *d = DEF(node->typ);
  const bool is_enum = d->which == DEFTYPE && d->as.DEFTYPE.kind == DEFTYPE_ENUM;

  if (node->flags & NODE_IS_TYPE) {
    print_typ(out, mod, node->typ);
  } else if ((is_enum && (node->flags & NODE_IS_DEFCHOICE))
             || (node->flags & NODE_IS_GLOBAL_LET)
             || (base->flags & NODE_IS_TYPE)) {
    bare_print_typ(out, mod, base->typ);
    pf(out, "$%s", name_s);
  } else {
    bool need_cast = false;
    const char *deref = ".";
    if (typ_is_dyn(base->typ) && typ_is_counted_reference(base->typ)) {
      need_cast = true;
      deref = ".ref.ref)->";
    } else if (typ_is_counted_reference(base->typ)) {
      need_cast = true;
      deref = ".ref)->";
    } else if (typ_is_reference(base->typ)) {
      deref = "->";
    }

    if (need_cast) {
      pf(out, "((");
      print_typ(out, mod, typ_generic_arg_const(base->typ, 0));
      pf(out, "*) ");
    }
    print_expr(out, mod, base, op);
    pf(out, "%s", deref);
    if (node->flags & NODE_IS_DEFCHOICE) {
      print_union_access_path(out, mod, base->typ, node_ident(name));
    } else {
      pf(out, "%s", name_s);
    }
  }
}

static void print_bin_isa(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *left = subs_first_const(node);
  const struct node *right = subs_last_const(node);

  pf(out, "n$reflect$Isa((void *)(");
  if (left->flags & NODE_IS_TYPE) {
    const struct typ *t = typ_is_dyn(left->typ) ? typ_generic_arg(left->typ, 0) : left->typ;
    pf(out, "&");
    bare_print_typ_actual(out, mod, t);
    pf(out, "$Reflect_type)");
  } else {
    print_expr(out, mod, left, T__STATEMENT);
    pf(out, ").dyntable");
  }
  pf(out, ", (void *)&");
  bare_print_typ_actual(out, mod, right->typ);
  pf(out, "$Reflect_type)");
}

static void print_bin(struct out *out, const struct module *mod, const struct node *node, uint32_t parent_op) {
  const uint32_t op = node->as.BIN.operator;

  if (!OP_IS_ASSIGN(op)) {
    pf(out, "(");
  }

  switch (OP_KIND(op)) {
  case OP_BIN:
  case OP_BIN_SYM:
  case OP_BIN_SYM_BOOL:
  case OP_BIN_SYM_ADDARITH:
  case OP_BIN_SYM_ARITH:
  case OP_BIN_SYM_INTARITH:
  case OP_BIN_SYM_OVARITH:
  case OP_BIN_SYM_BW:
  case OP_BIN_SYM_PTR:
  case OP_BIN_BW_RHS_UNSIGNED:
  case OP_BIN_OVBW_RHS_UNSIGNED:
    print_bin_sym(out, mod, node, parent_op);
    break;
  case OP_BIN_ACC:
    print_bin_acc(out, mod, node, parent_op);
    break;
  case OP_BIN_RHS_TYPE:
    if (op == Tisa) {
      print_bin_isa(out, mod, node);
    } else {
      print_typeconstraint(out, mod, node);
    }
    break;
  }

  if (!OP_IS_ASSIGN(op)) {
    pf(out, ")");
  }
}

static void print_un(struct out *out, const struct module *mod, const struct node *node, uint32_t parent_op) {
  const uint32_t op = node->as.UN.operator;
  const struct node *term = subs_first_const(node);

  pf(out, "(");

  switch (OP_KIND(op)) {
  case OP_UN_PRIMITIVES:
    switch (op) {
    case T__NULLABLE:
    case T__NONNULLABLE:
      print_expr(out, mod, term, parent_op);
      break;
    default:
      assert(false);
    }
    break;
  case OP_UN_OPT:
    switch (op) {
    case TPOSTQMARK:
      if (typ_is_dyn(term->typ)) {
        pf(out, "(");
        print_expr(out, mod, term, parent_op);
        pf(out, ").ref");
        if (typ_is_counted_reference(term->typ)) {
          pf(out, ".ref");
        }
        pf(out, " != NULL");
      } else if (typ_is_counted_reference(term->typ)) {
        pf(out, "(");
        print_expr(out, mod, term, parent_op);
        pf(out, ").ref");
        pf(out, " != NULL");
      } else if (typ_is_reference(term->typ)) {
        pf(out, "(");
        print_expr(out, mod, term, parent_op);
        pf(out, ") != NULL");
      } else {
        pf(out, "(");
        print_expr(out, mod, term, parent_op);
        pf(out, ").Nonnil");
      }
      break;
    case T__DEOPT:
      pf(out, "(");
      print_expr(out, mod, term, parent_op);
      pf(out, ").X");
      break;
    case TPREQMARK:
      if (typ_isa(node->typ, TBI_TRIVIAL_COPY)) {
        pf(out, "(");
        print_typ(out, mod, node->typ);
        pf(out, "){ .X = (");
        print_expr(out, mod, term, parent_op);
        pf(out, "), .Nonnil = 1 }");
      } else {
        assert(false && "should have been converted into an INIT");
      }
      break;
    default:
      assert(false);
    }
    break;
  case OP_UN_REFOF:
    if (node->flags & NODE_IS_TYPE) {
      print_typ(out, mod, node->typ);
    } else {
      pf(out, "(&");
      print_expr(out, mod, term, op);
      pf(out, ")");
    }
    break;
  case OP_UN_DEREF:
    if (node->flags & NODE_IS_TYPE) {
      print_typ(out, mod, term->typ);
    } else if (typ_is_counted_reference(term->typ)) {
      pf(out, "(*(");
      print_expr(out, mod, term, op);
      pf(out, ").ref)");
    } else {
      pf(out, "(*");
      print_expr(out, mod, term, op);
      pf(out, ")");
    }
    break;
  case OP_UN_BOOL:
  case OP_UN_ADDARITH:
  case OP_UN_OVARITH:
  case OP_UN_BW:
    print_token(out, op);
    print_expr(out, mod, term, op);
    break;
  }

  pf(out, ")");
}

static void print_tuple(struct out *out, const struct module *mod, const struct node *node, uint32_t parent_op) {
  if (node->flags & NODE_IS_TYPE) {
    print_typ(out, mod, node->typ);
    return;
  }

  pf(out, "{");
  size_t n = 0;
  FOREACH_SUB_CONST(s, node) {
    if (n > 0) {
      pf(out, ", ");
    }
    print_expr(out, mod, s, TCOMMA);
    n += 1;
  }
  pf(out, "}");
}

static void arg_is_vararg_slice_or_ref(bool *fwd_slice, bool *fwd_ref,
                                       const struct node *arg) {
  if (arg->which == CALLNAMEDARG && arg->as.CALLNAMEDARG.is_slice_vararg) {
    if (typ_is_reference(arg->typ)) {
      const struct typ *va = typ_generic_arg_const(arg->typ, 0);
      if (typ_generic_arity(va) > 0
          && typ_equal(typ_generic_functor_const(va), TBI_VARARG)) {
        *fwd_ref = true;
      }
    }

    *fwd_slice = !*fwd_ref;
  }
}

static void print_call_vararg_count(bool *did_retval_throughref,
                                    struct out *out, const struct module *mod, const struct node *dfun,
                                    const struct node *node, const struct node *arg,
                                    size_t n) {
  const ssize_t first_vararg = node_fun_first_vararg(dfun);
  if (n != first_vararg) {
    return;
  }

  // Must print return-through-ref first.
  if (node->as.CALL.return_through_ref_expr != NULL) {
    *did_retval_throughref = true;

    pf(out, "&(");
    print_expr(out, mod, node->as.CALL.return_through_ref_expr, T__CALL);
    pf(out, "), ");
  }

  const ssize_t count = subs_count(node) - 1 - first_vararg;
  bool fwd_slice = false, fwd_ref = false;
  arg_is_vararg_slice_or_ref(&fwd_slice, &fwd_ref, arg);

  if (count == 1 && (fwd_ref || fwd_slice)) {
    if (fwd_slice) {
      pf(out, "NLANG_BUILTINS_VACOUNT_SLICE");
    } else if (fwd_ref) {
      pf(out, "NLANG_BUILTINS_VACOUNT_VARARGREF");
    }
  } else {
    pf(out, "%zd", count);

    const struct node *last_arg = subs_last_const(node);
    bool last_fwd_slice = false, last_fwd_ref = false;
    arg_is_vararg_slice_or_ref(&last_fwd_slice, &last_fwd_ref, last_arg);
    if (last_fwd_slice) {
      pf(out, "|NLANG_BUILTINS_VACOUNT_LAST_IS_FWD_SLICE");
    } else if (last_fwd_ref) {
      pf(out, "|NLANG_BUILTINS_VACOUNT_LAST_IS_FWD_REF");
    }
  }

  if (count != 0) {
    pf(out, ", ");
  }
}

static const struct node *call_dyn_expr(const struct node *node) {
  const struct node *fun = subs_first_const(node);
  const struct typ *tfun = fun->typ;
  if (typ_definition_which(tfun) == DEFFUN) {
    return subs_first_const(subs_first_const(node));
  } else {
    return subs_at_const(node, 1);
  }
}

static void print_call_fun(struct out *out, const struct module *mod,
                           const struct node *node) {
  const struct node *fun = subs_first_const(node);
  const struct typ *tfun = fun->typ;
  struct tit *it = typ_definition_parent(tfun);
  if (tit_which(it) != DEFINTF) {
    // Normal case.
    tit_next(it);
    print_typ(out, mod, tfun);
    return;
  }

  // Dyn case.
  tit_next(it);
  print_expr(out, mod, call_dyn_expr(node), T__CALL);
  pf(out, ".dyntable->%s", idents_value(mod->gctx, typ_definition_ident(tfun)));
}

// For arguments passed by-value, the caller makes a copy and passes it in
// to the callee, which becomes responsible for it.
//
// These two principles must apply:
// - callee must dtor if non-`Trivial_dtor;
// - pass throug a ref if non-`Trivial_move.
//
// To keep things simple, we reuse `Return_by_copy (which we could rename to
// pass-by-copy). Anything non-`Return_by_copy that is passed by-value will
// be dtor by the callee and passed through a ref in C.
static bool callconv_callees(const struct typ *t) {
  return !typ_isa(t, TBI_RETURN_BY_COPY);
}

#define CALLCONV_CALLEES_PREFIX "_$Ncallees_"

static void print_defer_dtor_callees(struct out *out, const struct module *mod,
                                     const struct node *arg) {
  if (typ_is_counted_reference(arg->typ)) {
    pf(out, "NLANG_DEFER_VIA_CREF(");
  } else {
    pf(out, "NLANG_DEFER_VIA_REF(");
  }

  if (typ_is_counted_reference(arg->typ) && typ_is_dyn(arg->typ)) {
    pf(out, "n$builtins$__cdyn_dtor, ");
  } else if (typ_is_counted_reference(arg->typ)) {
    pf(out, "n$builtins$__cref_dtor, ");
  } else {
    struct tit *it = typ_definition_one_member(arg->typ, ID_DTOR);
    print_typ(out, mod, tit_typ(it));
    tit_next(it);
    pf(out, "__$Ndynwrapper, " CALLCONV_CALLEES_PREFIX);
  }

  print_expr(out, mod, subs_first_const(arg), T__CALL);
  pf(out, ");\n");
}

static void print_call(struct out *out, const struct module *mod,
                       const struct node *node, uint32_t parent_op) {
  const struct node *fun = subs_first_const(node);
  const struct typ *tfun = fun->typ;
  assert(typ_is_concrete(tfun));
  const struct node *dfun = DEF(tfun);
  const struct node *parentd = parent_const(dfun);

  const ident name = node_ident(dfun);
  if (((dfun->which == DEFFUN && dfun->as.DEFFUN.is_newtype_converter)
       || (dfun->which == DEFMETHOD && dfun->as.DEFMETHOD.is_newtype_converter))
      && parentd->which == DEFTYPE && parentd->as.DEFTYPE.newtype_expr != NULL) {
    const struct node *arg = subs_at_const(node, 1);
    if (typ_is_reference(arg->typ)) {
      pf(out, "*");
    }
    pf(out, "(");
    print_expr(out, mod, subs_at_const(node, 1), T__CALL);
    pf(out, ")");
    return;
  }
  if (name == ID_CAST) {
    pf(out, "(");
    print_typ(out, mod, node->typ);
    pf(out, ")(");
    const struct node *expr = subs_at_const(node, 1);
    if (typ_is_dyn(expr->typ) || typ_is_counted_reference(expr->typ)) {
      pf(out, "(");
      print_expr(out, mod, expr, T__CALL);
      pf(out, ").ref");
      if (typ_is_dyn(expr->typ) && typ_is_counted_reference(expr->typ)) {
        pf(out, ".ref");
      }
    } else {
      print_expr(out, mod, expr, T__CALL);
    }
    pf(out, ")");
    return;
  } else if (name == ID_DEBUG_REFERENCE_COUNT) {
    const struct node *expr = subs_at_const(node, 1);
    pf(out, "(");
    print_typ(out, mod, node->typ);
    pf(out, ")((");
    print_expr(out, mod, expr, T__CALL);
    pf(out, ").cnt == NULL ? 0 : (*(");
    print_expr(out, mod, expr, T__CALL);
    pf(out, ").cnt - 1))");
    return;
  } else if (name == ID_UNSAFE_ACQUIRE) {
    const struct node *expr = subs_at_const(node, 1);
    const struct node *rtr = node->as.CALL.return_through_ref_expr;
    if (typ_is_dyn(expr->typ)) {
      pf(out, "NLANG_CDYN_Acquire(");
    } else {
      pf(out, "NLANG_CREF_Acquire(");
    }
    print_expr(out, mod, expr, T__CALL);
    pf(out, ", &(");
    print_expr(out, mod, rtr, T__CALL);
    pf(out, "))");
    return;
  } else if (name == ID_UNSAFE_RELEASE) {
    const struct node *expr = subs_at_const(node, 1);
    if (typ_is_dyn(expr->typ)) {
      pf(out, "NLANG_CDYN_Release(");
    } else {
      pf(out, "NLANG_CREF_Release(");
    }
    print_typ(out, mod, node->typ);
    pf(out, ", &(");
    print_expr(out, mod, expr, T__CALL);
    pf(out, "))");
    return;
  } else if ((name == ID_COPY_CTOR || name == ID_MOVE || name == ID_DTOR)
             && typ_is_reference(subs_at_const(node, 1)->typ)
             && typ_is_counted_reference(typ_generic_arg(subs_at_const(node, 1)->typ, 0))
             && typ_is_dyn(typ_generic_arg(subs_at_const(node, 1)->typ, 0))) {
    const struct node *self = subs_at_const(node, 1);
    pf(out, "NLANG_CDYN_%s(", idents_value(mod->gctx, name));
    print_expr(out, mod, self, T__CALL);
    if (name == ID_COPY_CTOR) {
      pf(out, ", ");
      print_expr(out, mod, subs_at_const(node, 2), T__CALL);
    } else if (name == ID_MOVE) {
      pf(out, ", &(");
      print_expr(out, mod, node->as.CALL.return_through_ref_expr, T__CALL);
      pf(out, ")");
    }
    pf(out, ")");
    return;
  } else if (name == ID_DYNCAST) {
    const struct typ *i = typ_generic_arg_const(tfun, 0);
    const struct node *arg = subs_at_const(node, 1);
    if (typ_definition_which(i) == DEFINTF) {
      // Result is dyn.
      if (typ_is_counted_reference(arg->typ)) {
        const struct node *rtr = node->as.CALL.return_through_ref_expr;
        pf(out, "(");
        print_expr(out, mod, rtr, T__CALL);
        pf(out, ") = NLANG_MKCDYN(struct _$Ncdyn_");
      } else {
        pf(out, "NLANG_MKDYN(struct _$Ndyn_");
      }
      bare_print_typ(out, mod, i);
      pf(out, ", n$reflect$Get_dyntable_for((void *)(");
      print_expr(out, mod, arg, T__CALL);
      pf(out, ").dyntable, (void *)&");
      bare_print_typ_actual(out, mod, i);
      pf(out, "$Reflect_type), (");
      print_expr(out, mod, arg, T__CALL);
      pf(out, ").ref)");
    } else {
      // Result is DEFTYPE.
      pf(out, "(n$reflect$Get_dyntable_for((void *)(");
      print_expr(out, mod, arg, T__CALL);
      pf(out, ").dyntable, (void *)&");
      bare_print_typ_actual(out, mod, i);
      pf(out, "$Reflect_type) != NULL ? (");
      if (typ_is_counted_reference(arg->typ)) {
        pf(out, "NLANG_CREF_CAST(");
        print_typ(out, mod, node->typ);
        pf(out, ", NLANG_CREF_INCR((");
        print_expr(out, mod, arg, T__CALL);
        pf(out, ").ref))) : (");
        print_typ(out, mod, node->typ);
        pf(out, ") { 0 })");
      } else {
        print_typ(out, mod, i);
        pf(out, " *)(");
        print_expr(out, mod, arg, T__CALL);
        pf(out, ").ref : NULL)");
      }
    }
    return;
  } else if (name == ID_NEXT && typ_isa(parentd->typ, TBI_VARARG)) {
    const struct node *self = subs_at_const(node, 1);
    if (typ_is_dyn(typ_generic_arg(parentd->typ, 0))) {
      pf(out, "NLANG_BUILTINS_VARARG_NEXT_DYN(");
    } else {
      pf(out, "NLANG_BUILTINS_VARARG_NEXT(");
    }
    print_typ(out, mod, typ_generic_arg_const(parentd->typ, 0));
    pf(out, ", ");
    print_typ(out, mod, typ_function_return_const(subs_first_const(node)->typ));
    pf(out, ", ");
    if (typ_is_reference(self->typ)) {
      pf(out, "*");
    }
    pf(out, "(");
    print_expr(out, mod, self, T__CALL);
    pf(out, "))");
    return;
  }

  const ssize_t first_vararg = node_fun_first_vararg(dfun);

  print_call_fun(out, mod, node);
  pf(out, "(");

  size_t n = 1;
  bool force_comma = false;

  if (parentd->which == DEFINTF) {
    if (dfun->which == DEFMETHOD) {
      const struct node *a = call_dyn_expr(node);
      print_expr(out, mod, a, T__CALL);
      pf(out, ".ref");
      if (typ_is_counted_reference(a->typ)
          && !typ_is_counted_reference(typ_function_arg_const(tfun, 0))) {
        pf(out, ".ref");
      }
      n = 2;
    }
  }

  bool did_retval_throughref = false;
  FOREACH_SUB_EVERY_CONST(arg, node, n, 1) {
    if (force_comma || n > 1) {
      pf(out, ", ");
    }

    print_call_vararg_count(&did_retval_throughref, out, mod, dfun, node, arg, n - 1);

    if ((dfun->which != DEFMETHOD || n > 1 /* i.e. not for self */)
        && callconv_callees(arg->typ)) {
      pf(out, "&");
    }

    print_expr(out, mod, arg, T__CALL);
    n += 1;
  }

  if ((ssize_t)subs_count(node) - 1 <= first_vararg) {
    if (n > 1) {
      pf(out, ", ");
    }
    pf(out, "0");
  }

  if (node->as.CALL.return_through_ref_expr != NULL
      && !did_retval_throughref) {
    if (n > 1) {
      pf(out, ", ");
    }

    pf(out, "&(");
    print_expr(out, mod, node->as.CALL.return_through_ref_expr, T__CALL);
    pf(out, ")");
  } else {
    assert(did_retval_throughref || typ_isa(node->typ, TBI_RETURN_BY_COPY));
  }

  pf(out, ")");
}

static void print_init_array(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *par = parent_const(node);
  if (par->which == BIN && par->as.BIN.operator == TASSIGN) {
    const struct node *target = node->as.INIT.target_expr;
    print_expr(out, mod, target, T__STATEMENT);
  }

  if (!subs_count_atleast(node, 1)) {
    pf(out, "{ 0 }");
    return;
  }

  const struct node *el = subs_first_const(node);

  pf(out, "{ (");
  print_typ(out, mod, el->typ);
  pf(out, "[]){ ");

  FOREACH_SUB_CONST(s, node) {
    print_expr(out, mod, s, T__NOT_STATEMENT);
    pf(out, ", ");
  }
  pf(out, " }, %zu, %zu }", subs_count(node), subs_count(node));
}

static void print_tag_init(struct out *out, const struct module *mod,
                           const struct node *node, bool is_inline) {
  assert(node->which == INIT);
  const struct node *d = DEF(node->typ);
  if (d->which != DEFTYPE || d->as.DEFTYPE.kind != DEFTYPE_UNION) {
    return;
  }

  const ident tag = node->as.INIT.for_tag;
  if (tag == ID__NONE) {
    // We rely on constraints to catch the cases where this would create a
    // badly initialized enum/union. But in a case like:
    //  let what such
    //    if ...
    //      what = A
    //    else
    //      what = B
    // This is overall well-formed.
    return;
  }

  const struct node *ch = node_get_member_const(d, tag);

  if (!is_inline) {
    pf(out, ";\n");
    print_expr(out, mod, node->as.INIT.target_expr, TDOT);
  }
  pf(out, ".%s = ", idents_value(mod->gctx, ID_TAG));
  print_defchoice_path(out, mod, d, ch);
  pf(out, "$%s", idents_value(mod->gctx, ID_TAG));
  if (!is_inline) {
    pf(out, ";\n");
  }
}

static void print_init_toplevel(struct out *out, const struct module *mod,
                                const struct node *node) {
  if (!subs_count_atleast(node, 1)) {
    pf(out, "{ 0 }");
    return;
  }

  // FIXME: unions unsupported (except trivial init)
  assert(subs_count(node) == 0 || node->as.INIT.for_tag == ID__NONE);

  pf(out, "{\n");
  print_tag_init(out, mod, node, true);
  FOREACH_SUB_EVERY_CONST(s, node, 0, 2) {
    pf(out, ".");
    pf(out, "%s", idents_value(mod->gctx, node_ident(s)));
    pf(out, " = ");
    print_expr(out, mod, next_const(s), T__NOT_STATEMENT);
    pf(out, ",\n");
  }
  pf(out, " }");
}

static void print_init(struct out *out, const struct module *mod,
                       const struct node *node) {
  if (node->as.INIT.is_array) {
    print_init_array(out, mod, node);
    return;
  }

  if (node->flags & (NODE_IS_GLOBAL_STATIC_CONSTANT | NODE_IS_LOCAL_STATIC_CONSTANT)) {
    print_init_toplevel(out, mod, node);
    return;
  }

  if (typ_equal(node->typ, TBI_VOID)) {
    return;
  }

  const struct node *par = parent_const(node);
  if (par->which == DEFNAME || par->which == BLOCK) {
    pf(out, "{ 0 }");
  } else {
    print_expr(out, mod, node->as.INIT.target_expr, TDOT);
    pf(out, " = (");
    print_typ(out, mod, node->typ);
    pf(out, "){ 0 }");
  }

  print_tag_init(out, mod, node, false);

  // FIXME: hierarchical unions unsupported

  if (node->as.INIT.is_defchoice_external_payload_constraint) {
    pf(out, ";\n");
    // Any initialization of the union member is done by an assign, see
    // step_flatten_init.
    return;
  }

  FOREACH_SUB_EVERY_CONST(s, node, 0, 2) {
    pf(out, ";\n");
    print_expr(out, mod, node->as.INIT.target_expr, TDOT);
    pf(out, ".");
    print_union_init_access_path(out, mod, node);
    pf(out, "%s", idents_value(mod->gctx, node_ident(s)));
    pf(out, " = ");
    print_expr(out, mod, next_const(s), T__NOT_STATEMENT);
  }
}

static void print_deftype_name(struct out *out, const struct module *mod, const struct node *node) {
  bare_print_typ_actual(out, mod, node->typ);
}

static void print_deffun_name(struct out *out, const struct module *mod, const struct node *node) {
  const ident id = node_ident(node);
  if (id == ID_MAIN) {
    pf(out, "_$Nmain");
  } else {
    print_typ(out, mod, node->typ);
  }
}

static void print_deffield_name(struct out *out, const struct module *mod, const struct node *node) {
  const ident id = node_ident(node);
  pf(out, "%s", idents_value(mod->gctx, id));
}

static void print_ident_locally_shadowed(struct out *out, const struct node *def) {
  if (def != NULL && def->which == DEFNAME && def->as.DEFNAME.locally_shadowed != 0) {
    pf(out, "$%zu", def->as.DEFNAME.locally_shadowed);
  }
}

static void print_ident_arg_prefix(struct out *out, const struct module *mod,
                                   const struct node *defarg) {
  assert(defarg->which == DEFARG);
  const struct node *fun = nparent_const(defarg, 2);
  const bool is_self = fun->which == DEFMETHOD && prev_const(defarg) == NULL;
  const bool is_vararg = defarg->as.DEFARG.is_vararg;
  if (!is_self && !is_vararg) {
    // Arguments (including return values) are accessed through a macro,
    // which name starts with _Narg_, to avoid conflicts with anything in
    // a subscope, especially names of fields in accessors.
    pf(out, "_Narg_");
  }
}

static void print_ident(struct out *out, const struct module *mod, const struct node *node) {
  assert(node->which == IDENT);
  assert(node_ident(node) != ID_ANONYMOUS);

  if (node->as.IDENT.non_local_scoper != NULL) {
    print_scope_name(out, mod, node->as.IDENT.non_local_scoper);
    pf(out, "$");
  }

  const struct node *def = node->as.IDENT.def;
  if (def != NULL && def->which == DEFARG) {
    print_ident_arg_prefix(out, mod, def);
  }

  print_scope_last_name(out, mod, node);

  print_ident_locally_shadowed(out, node->as.IDENT.def);
}

static void print_conv(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *arg = subs_first_const(node);

  if (typ_is_dyn(node->typ) && typ_is_dyn(arg->typ)) {
    const struct typ *intf = typ_generic_arg_const(node->typ, 0);
      if (typ_is_counted_reference(node->typ)) {
        assert(typ_is_counted_reference(arg->typ));
        pf(out, "NLANG_MKCDYN(struct _$Ncdyn_");
      } else {
        pf(out, "NLANG_MKDYN(struct _$Ndyn_");
      }
      bare_print_typ(out, mod, intf);
      pf(out, ", n$reflect$Get_dyntable_for((void *)(");
      print_expr(out, mod, arg, T__CALL);
      pf(out, ").dyntable, (void *)&");
      bare_print_typ_actual(out, mod, intf);
      pf(out, "$Reflect_type), (");
      print_expr(out, mod, arg, T__CALL);
      pf(out, ").ref");
      if (typ_is_counted_reference(arg->typ) && !typ_is_counted_reference(node->typ)) {
        pf(out, ".ref");
      }
      pf(out, ")");
      return;

  } else if (typ_is_dyn(node->typ)) {
    const struct typ *intf = typ_generic_arg_const(node->typ, 0);
    const struct typ *concrete = typ_generic_arg_const(arg->typ, 0);
    if (typ_is_counted_reference(node->typ)) {
      assert(typ_is_counted_reference(arg->typ));
      pf(out, "NLANG_MKCDYN(struct _$Ncdyn_");
    } else {
      pf(out, "NLANG_MKDYN(struct _$Ndyn_");
    }
    bare_print_typ(out, mod, intf);
    pf(out, ", &");
    bare_print_typ_actual(out, mod, concrete);
    pf(out, "$Dyntable__");
    bare_print_typ_actual(out, mod, intf);
    pf(out, ", ");
    if (!typ_is_counted_reference(arg->typ)) {
      pf(out, "(void *)");
    }
    pf(out, "(");
    print_expr(out, mod, arg, T__CALL);
    pf(out, ")");
    if (typ_is_counted_reference(arg->typ) && !typ_is_counted_reference(node->typ)) {
      pf(out, ".ref");
    }
    pf(out, ")");
    return;

  } else if (typ_is_counted_reference(arg->typ)) {
    assert(!typ_is_counted_reference(node->typ));
    pf(out, "(void *)(");
    print_expr(out,mod, arg, T__CALL);
    pf(out, ").ref");

  } else {
    assert(false && "unreached");
  }
}

static void print_expr(struct out *out, const struct module *mod, const struct node *node, uint32_t parent_op) {
  switch (node->which) {
  case NIL:
    if (typ_is_reference(node->typ)
        && !typ_is_counted_reference(node->typ)
        && !typ_is_dyn(node->typ)) {
      pf(out, "NULL");
    } else {
      pf(out, "(");
      print_typ(out, mod, node->typ);
      pf(out, "){ 0 }");
    }
    break;
  case IDENT:
    print_ident(out, mod, node);
    break;
  case NUMBER:
    if (typ_isa(node->typ, TBI_FLOATING)) {
      pf(out, "%s", node->as.NUMBER.value);
    } else if (typ_isa(node->typ, TBI_UNSIGNED_INTEGER)) {
      pf(out, "%sULL", node->as.NUMBER.value);
    } else {
      pf(out, "%sLL", node->as.NUMBER.value);
    }
    break;
  case BOOL:
    pf(out, "(");
    print_typ(out, mod, node->typ);
    pf(out, ")");
    pf(out, "%s", node->as.BOOL.value ? "1" : "0");
    break;
  case STRING:
    {
      char *s = escape_string(node->as.STRING.value);
      if (typ_equal(node->typ, TBI_STRING)) {
        pf(out, "NLANG_STRING_LITERAL(\"%s\")", s);
      } else {
        assert(false);
      }
      free(s);
    }
    break;
  case SIZEOF:
    pf(out, "sizeof(");
    print_typ(out, mod, subs_first_const(node)->typ);
    pf(out, ")");
    break;
  case ALIGNOF:
    pf(out, "__alignof__(");
    print_typ(out, mod, subs_first_const(node)->typ);
    pf(out, ")");
    break;
  case BIN:
    print_bin(out, mod, node, parent_op);
    break;
  case UN:
    print_un(out, mod, node, parent_op);
    break;
  case TYPECONSTRAINT:
    print_typeconstraint(out, mod, node);
    break;
  case CALL:
    print_call(out, mod, node, parent_op);
    break;
  case CALLNAMEDARG:
    print_expr(out, mod, subs_first_const(node), parent_op);
    break;
  case TUPLE:
    print_tuple(out, mod, node, parent_op);
    break;
  case INIT:
    print_init(out, mod, node);
    break;
  case CONV:
    print_conv(out, mod, node);
    break;
  case BLOCK:
    {
      print_block(out, mod, node);
    }
    break;
  case IF:
  case TRY:
  case ASSERT:
  case PRE:
  case POST:
  case INVARIANT:
    pf(out, "({ ");
    print_statement(out, mod, node);
    pf(out, "; })");
    break;
  default:
    fprintf(g_env.stderr, "Unsupported node: %d\n", node->which);
    assert(false);
  }
}

static void print_while(struct out *out, const struct module *mod, const struct node *node) {
  pf(out, "while (");
  print_expr(out, mod, subs_first_const(node), T__STATEMENT);
  pf(out, ")");
  print_block(out, mod, subs_at_const(node, 1));
}

static void print_if(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *n = subs_first_const(node);
  const size_t count = subs_count(node);
  const size_t br_count = count / 2 + count % 2;

  print_block(out, mod, n);
  pf(out, " ? ");
  n = next_const(n);
  print_block(out, mod, n);

  if (br_count == 1) {
    pf(out, " : ({;}) )");
    return;
  }

  n = next_const(n);
  while (n != NULL && next_const(n) != NULL) {
    pf(out, "\n");
    pf(out, " : ");
    print_expr(out, mod, n, T__STATEMENT);
    pf(out, " ? ");
    n = next_const(n);
    print_block(out, mod, n);
    n = next_const(n);
  }

  if (n != NULL) {
    pf(out, "\n");
    pf(out, " : ");
    print_block(out, mod, n);
  } else {
    pf(out, " : ({;})");
  }
}

static void print_defchoice_path(struct out *out,
                                 const struct module *mod,
                                 const struct node *deft,
                                 const struct node *ch) {
  print_deftype_name(out, mod, deft);

  if (ch == deft) {
    return;
  }

  pf(out, "$");
  print_deffield_name(out, mod, ch);
}

static void print_try(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *err = subs_at_const(node, 1);
  print_statement(out, mod, err);
  pf(out, ";\n");

  const struct node *eblock = subs_last_const(node);
  print_block(out, mod, subs_first_const(eblock));

  pf(out, "while (0) {\n");
  FOREACH_SUB_EVERY_CONST(catch, eblock, 1, 1) {
    pf(out, "\n%s: {\n", idents_value(mod->gctx, catch->as.CATCH.label));
    print_block(out, mod, subs_first_const(catch));
    pf(out, "\n}\n");
  }
  pf(out, "}\n");
}

static void print_assert(struct out *out, const struct module *mod, const struct node *node) {
  print_block(out, mod, subs_first_const(node));
}

static void print_pre(struct out *out, const struct module *mod, const struct node *node) {
  print_block(out, mod, subs_first_const(node));
}

static void print_post(struct out *out, const struct module *mod, const struct node *node) {
  print_block(out, mod, subs_first_const(node));
}

static void print_invariant(struct out *out, const struct module *mod, const struct node *node) {
  print_block(out, mod, subs_first_const(node));
}

static void print_generic_fun_linkage(struct out *out, bool header, enum forward fwd,
                                      const struct node *node) {
  if (node_is_inline(node)) {
    pf(out, ALWAYS_INLINE " static inline ");
  } else {
    if (fwd == FWD_DECLARE_FUNCTIONS) {
      pf(out, WEAK " ");
    }
  }
}

static void print_fun_linkage(struct out *out, bool header, enum forward fwd,
                              const struct node *node) {
  if (typ_generic_arity(node->typ) > 0
      || (!node_is_at_top(node) && typ_generic_arity(parent_const(node)->typ) > 0)) {
    print_generic_fun_linkage(out, header, fwd, node);
    return;
  }

  const struct toplevel *toplevel = node_toplevel_const(node);
  const uint32_t flags = toplevel->flags;

  if (((flags & TOP_IS_EXPORT) && (flags & TOP_IS_INLINE))
      || ((flags & TOP_IS_EXTERN) && (flags & TOP_IS_INLINE))) {
    pf(out, ALWAYS_INLINE " static inline ");
  } else if ((flags & TOP_IS_EXTERN) && (flags & TOP_IS_EXPORT)) {
    pf(out, "extern ");
  } else if (flags & TOP_IS_EXPORT) {
    // noop
  } else if (flags & TOP_IS_FORCE_SYMBOL_EXPORT) {
    // noop
  } else {
    pf(out, UNUSED " static ");
  }
}

static const struct typ *intercept_slices(const struct module *mod, const struct typ *t) {
  const struct node *d = DEF(t);

  if (node_is_at_top(d) && typ_generic_arity(t) == 0) {
    return t;
  }

  if (!node_is_at_top(d) && NM(d->which) & (NM(DEFFUN) | NM(DEFMETHOD))) {
    const struct node *pd = parent_const(d);
    const struct typ *pt = intercept_slices(mod, pd->typ);
    if (pt == pd->typ) {
      return t;
    }

    const struct node *m = node_get_member_const(DEF(pt),
                                                 node_ident(d));
    const struct typ *mt = m->typ;
    if (typ_generic_arity(mt) == 0) {
      return mt;
    }

    const size_t arity = typ_generic_arity(t);
    struct typ **args = calloc(arity, sizeof(struct typ *));
    for (size_t a = 0; a < arity; ++a) {
      args[a] = typ_generic_arg(CONST_CAST(t), a);
    }

    struct typ *r = instances_find_existing_final_with(CONST_CAST(m)->typ, args, arity);
    assert(r);
    free(args);
    return r;
  }

  if (!typ_is_slice(t)) {
    return t;
  }

  if (d->which != DEFTYPE) {
    return t;
  }

  if (typ_is_generic_functor(t)) {
    return TBI_SLICE_IMPL;
  } else {
    const char name[] = "create_impl_instance";
    const ident nameid = idents_add_string(mod->gctx, name, ARRAY_SIZE(name)-1);
    const struct node *m = node_get_member_const(d, nameid);
    return node_fun_retval_const(m)->typ;
  }
}

static void print_typ_name(struct out *out, const struct module *mod,
                           const struct typ *t, bool newtype_override) {
  if (typ_generic_arity(t) > 0 && !typ_is_generic_functor(t)) {
    print_typ_name(out, mod, typ_generic_functor_const(t), newtype_override);
    return;
  }

  t = intercept_slices(mod, t);

  const struct node *d = DEF(t);
  while (!newtype_override && d->which == DEFTYPE && d->as.DEFTYPE.newtype_expr != NULL) {
    d = DEF(d->as.DEFTYPE.newtype_expr->typ);
  }

  print_scope_name(out, mod, d);
}

static bool fun_is_newtype_pretend_wrapper(const struct node *node) {
  return (node->which == DEFFUN && node->as.DEFFUN.is_newtype_pretend_wrapper)
          || (node->which == DEFMETHOD && node->as.DEFMETHOD.is_newtype_pretend_wrapper);
}

static bool newtype_has_local_override(const struct module *mod, const struct typ *typ) {
  const struct node *def = DEF(typ);
  const struct node *par = parent_const(def);
  if (par->which == DEFTYPE && par->as.DEFTYPE.newtype_expr != NULL) {
    struct node *existing = NULL;
    error ignore = scope_lookup_ident_immediate(
      &existing, NULL, mod, par, node_ident(def), true);
    (void) ignore;
    return existing != NULL && !fun_is_newtype_pretend_wrapper(existing);
  }
  return false;
}

static void print_typ_function(struct out *out, const struct module *mod, const struct typ *typ) {
  const struct node *def = DEF(typ);

  if (typ_generic_arity(typ) > 0) {
    pf(out, "_$Ngen_");
  }

  if (node_is_at_top(def)) {
    print_typ_name(out, mod, typ, false);
  } else {
    const struct node *par = parent_const(def);
    const bool newtype_override = newtype_has_local_override(mod, typ);
    if (newtype_override) {
      print_typ_name(out, mod, par->typ, newtype_override);
    } else {
      bare_print_typ(out, mod, par->typ);
    }

    if (par->which == DEFCHOICE) {
      pf(out, "$%s", idents_value(mod->gctx, node_ident(par)));
    }
    pf(out, "$%s", idents_value(mod->gctx, node_ident(def)));
  }

  if (typ_generic_arity(typ) > 0) {
    for (size_t n = 0; n < typ_generic_arity(typ); ++n) {
      pf(out, "$$");
      bare_print_typ(out, mod, typ_generic_arg_const(typ, n));
    }
    pf(out, "_genN$_");
  }
}

static void print_typ_data(struct out *out, const struct module *mod, const struct typ *typ,
                           bool newtype_override) {
  if (typ_is_generic_functor(typ)) {
    print_typ_name(out, mod, typ, false);
    return;
  } else if (typ_is_dyn(typ)) {
    if (typ_is_counted_reference(typ)) {
      pf(out, "_$Ncdyn_");
    } else {
      pf(out, "_$Ndyn_");
    }
    print_typ_data(out, mod, typ_generic_arg_const(typ, 0), newtype_override);
    return;
  } else if (typ_is_counted_reference(typ)
             || typ_generic_functor_const(typ) == TBI_CREF_IMPL) {
    pf(out, "_$Ncref_");
    print_typ_data(out, mod, typ_generic_arg_const(typ, 0), newtype_override);
    return;
  } else if (typ_is_reference(typ)) {
    pf(out, "_$Nref_");
    print_typ_data(out, mod, typ_generic_arg_const(typ, 0), newtype_override);
    return;
  }

  if (typ_generic_arity(typ) > 0) {
    pf(out, "_$Ngen_");
  }

  print_typ_name(out, mod, typ, newtype_override);

  if (typ_generic_arity(typ) > 0) {
    for (size_t n = 0; n < typ_generic_arity(typ); ++n) {
      pf(out, "$$");
      bare_print_typ(out, mod, typ_generic_arg_const(typ, n));
    }
    pf(out, "_genN$_");
  }
}

static void bare_print_typ_actual(struct out *out, const struct module *mod, const struct typ *typ) {
  if (typ_is_function(typ)) {
    print_typ_function(out, mod, typ);
  } else {
    print_typ_data(out, mod, typ, true);
  }
}

static void bare_print_typ(struct out *out, const struct module *mod, const struct typ *typ) {
  if (typ_is_function(typ)) {
    print_typ_function(out, mod, typ);
  } else {
    print_typ_data(out, mod, typ, false);
  }
}

static void print_typ(struct out *out, const struct module *mod, const struct typ *typ) {
  if (!typ_is_function(typ)
      && (typ_is_dyn(typ) || typ_is_counted_reference(typ) || !typ_is_reference(typ))
      && !typ_equal(typ, TBI_VOID)
      && (!typ_isa(typ, TBI_NATIVE) || typ_isa(typ, TBI_ANY_TUPLE))
      && (typ_definition_which(typ) != DEFTYPE
          || typ_definition_deftype_kind(typ) != DEFTYPE_ENUM)) {
    pf(out, "struct ");
  }

  bare_print_typ(out, mod, typ);
}

static void print_defname_name(struct out *out, const struct module *mod,
                               const struct node *node) {
  if (node->flags & NODE_IS_GLOBAL_LET) {
    const struct node *let = parent_const(node);
    if (node_is_at_top(let)) {
      print_scope_name(out, mod, parent_const(parent_const(node)));
    } else {
      bare_print_typ(out, mod, parent_const(let)->typ);
    }
    pf(out, "$");
  }
  print_expr(out, mod, subs_first_const(node), T__STATEMENT);
  print_ident_locally_shadowed(out, node);
}

static void forward_declare(struct out *out, const struct module *mod,
                            const struct typ *t) {
  if (typ_equal(t, TBI_VOID) || typ_isa(t, TBI_NATIVE)) {
    return;
  }

  if (typ_is_reference(t)) {
    const struct typ *tt = typ_generic_arg_const(t, 0);
    forward_declare(out, mod, tt);
    const struct node *dd = typ_definition_ignore_any_overlay_const(tt);
    pf(out, "#ifndef _$Nref_");
    print_deftype_name(out, mod, dd);
    pf(out, "\n#define _$Nref_");
    print_deftype_name(out, mod, dd);
    pf(out, " ");
    if (typ_is_counted_reference(t)) {
      pf(out, "struct ");
    }
    print_deftype_name(out, mod, dd);
    pf(out, "*\n#endif\n");
    return;
  }

  print_typ(out, mod, t);
  pf(out, ";\n");
}

static void print_defname_linkage(struct out *out, bool difft_mod, enum forward fwd,
                                  const struct module *mod,
                                  const struct node *let,
                                  const struct node *node,
                                  bool will_define) {
  if (!(node->flags & NODE_IS_GLOBAL_LET)) {
    return;
  }

  const struct node *at_top = node_is_at_top(let) ? let : parent_const(let);
  const bool in_gen = typ_generic_arity(at_top->typ) > 0;
  const struct toplevel *toplevel = node_toplevel_const(at_top);
  const uint32_t flags = toplevel->flags;

  if (will_define && in_gen) {
    pf(out, WEAK " ");
  }

  if (flags & TOP_IS_EXTERN) {
    pf(out, "extern ");
  } else if (difft_mod && !will_define) {
    pf(out, "extern ");
  } else if (!difft_mod && !(flags & TOP_IS_EXPORT)) {
    pf(out, UNUSED " static ");
  }
}

static void print_defer_dtor(struct out *out, const struct module *mod,
                             struct typ *t) {
  pf(out, " NLANG_DEFER(");

  if (typ_is_counted_reference(t) && typ_is_dyn(t)) {
    pf(out, "n$builtins$__cdyn_dtor");
  } else if (typ_is_counted_reference(t)) {
    pf(out, "n$builtins$__cref_dtor");
  } else {
    struct tit *it = typ_definition_one_member(t, ID_DTOR);
    print_typ(out, mod, tit_typ(it));
    tit_next(it);
  }

  pf(out, ") ");
}

static void print_defname_cleanup(struct out *out, const struct module *mod,
                                  const struct node *node) {
  if (node->flags & NODE_IS_GLOBAL_LET) {
    return;
  } else if (subs_last_const(node)->flags & NODE_IS_LOCAL_STATIC_CONSTANT) {
    return;
  } else if (typ_is_counted_reference(node->typ)) {
    // noop
  } else if (typ_isa(node->typ, TBI_TRIVIAL_DTOR)) {
    // Includes uncounted references.
    return;
  } else if (node->flags & NODE_IS_CALLEES) {
    return;
  } else if (node_ident(node) == ID_OTHERWISE) {
    // FIXME(e): This is wrong, and quite bad: anonymous variables should be
    // cleaned-up like any other, and for that reason we're going to have to
    // give them a gensym name.
    return;
  }

  print_defer_dtor(out, mod, node->typ);
}

static void print_defname(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node) {
  const struct node *let = parent_const(node);
  if (node->which == DEFALIAS) {
    if (fwd == FWD_DECLARE_FUNCTIONS
        && typ_definition_which(node->typ) == DEFTYPE && node_is_at_top(let)) {
      // Only defined for the benefit of C code written manually that
      // interacts with N.
      forward_declare(out, mod, node->typ);
      pf(out, "#ifndef ");
      print_scope_name(out, mod, parent_const(let));
      pf(out, "$%s", idents_value(mod->gctx, node_ident(node)));
      pf(out, "\n#define ");
      print_scope_name(out, mod, parent_const(let));
      pf(out, "$%s", idents_value(mod->gctx, node_ident(node)));
      pf(out, " ");
      print_typ(out, mod, node->typ);
      pf(out, "\n#endif\n");
    }
    return;
  }

  if (fwd != FWD_DECLARE_FUNCTIONS && fwd != FWD_DEFINE_FUNCTIONS) {
    return;
  }

  const struct node *expr = subs_last_const(node);
  if (node->flags & NODE_IS_GLOBAL_LET) {
    if (skip(header, fwd, let)) {
      return;
    }
  } else if (expr->flags & NODE_IS_LOCAL_STATIC_CONSTANT) {
    pf(out, " static ");
  } else if ((expr->which == UN && expr->as.UN.operator == T__NULLABLE)
             || (expr->which == UN && expr->as.UN.operator == T__NONNULLABLE)) {
    assert(subs_last_const(expr)->which == IDENT);
    pf(out, "#define %s %s\n",
       idents_value(mod->gctx, node_ident(node)),
       idents_value(mod->gctx, node_ident(subs_last_const(expr))));
    return;
  }

  print_defname_cleanup(out, mod, node);

  if (node_ident(node) == ID_OTHERWISE) {
    if (expr != NULL) {
      if (expr->which == BLOCK && !subs_count_atleast(expr, 2)) {
        expr = subs_first_const(expr);
      }
      pf(out, "(void) ");
      print_expr(out, mod, expr, T__STATEMENT);
    }
    return;
  }

  const struct node *par = parent_const(let);
  const bool in_gen = !node_is_at_top(let) && typ_generic_arity(par->typ) > 0;
  const bool global = node->flags & NODE_IS_GLOBAL_LET;
  const bool difft_mod = !is_in_topmost_module(let);
  const bool will_define = fwd == FWD_DEFINE_FUNCTIONS
    && (!global || !difft_mod || in_gen || node_is_inline(let))
    && !(node_toplevel_const(let)->flags & TOP_IS_EXTERN);

  const bool is_void = typ_equal(node->typ, TBI_VOID);
  if (!is_void) {
    print_defname_linkage(out, difft_mod, fwd, mod, let, node, will_define);

    if (node->as.DEFNAME.may_be_unused) {
      pf(out, UNUSED " ");
    }

    print_typ(out, mod, node->typ);
    pf(out, " ");
    print_defname_name(out, mod, node);
  }

  if (will_define) {
    if (expr != NULL) {
      if (expr->which == BLOCK && !subs_count_atleast(expr, 2)) {
        expr = subs_first_const(expr);
      }

      if (expr->which == INIT) {
        if (!is_void) {
          pf(out, " = ");
        }
        print_init(out, mod, expr);
      } else if (expr->which == CALL
                 && !typ_isa(expr->typ, TBI_RETURN_BY_COPY)) {
        if (!is_void) {
          pf(out, " = { 0 };\n");
        }
        print_expr(out, mod, expr, T__STATEMENT);
      } else {
        if (!is_void) {
          pf(out, " = ");
        }
        print_expr(out, mod, expr, T__STATEMENT);
      }
    } else {
      if (!is_void) {
        pf(out, " = { 0 }");
      }
    }
  }
}

static void print_return(struct out *out, const struct module *mod, const struct node *node) {
  if (subs_count_atleast(node, 1)) {
    const struct node *expr = subs_first_const(node);
    if (!node->as.RETURN.forced_return_through_ref
        && typ_isa(expr->typ, TBI_RETURN_BY_COPY)) {
      pf(out, "return ");
      print_expr(out, mod, expr, T__STATEMENT);
    } else {
      if (expr->which != IDENT) {
        print_expr(out, mod, expr, T__STATEMENT);
      }
      pf(out, ";\nreturn");
    }
  } else {
    pf(out, "return");
  }
}

static void print_statement(struct out *out, const struct module *mod, const struct node *node) {
  out->node = node;

  switch (node->which) {
  case LET:
    print_defname(out, false, FWD_DEFINE_FUNCTIONS, mod, subs_first_const(node));
    break;
  case RETURN:
    print_return(out, mod, node);
    break;
  case WHILE:
    print_while(out, mod, node);
    break;
  case BREAK:
    pf(out, "break");
    break;
  case CONTINUE:
    pf(out, "continue");
    break;
  case NOOP:
    break;
  case IF:
    print_if(out, mod, node);
    break;
  case TRY:
    print_try(out, mod, node);
    break;
  case ASSERT:
    print_assert(out, mod, node);
    break;
  case PRE:
    print_pre(out, mod, node);
    break;
  case POST:
    print_post(out, mod, node);
    break;
  case INVARIANT:
    print_invariant(out, mod, node);
    break;
  case IDENT:
    if (ident_is_spurious_ssa_var(node)) {
      break;
    }
    // fallthrough
  case NUMBER:
  case BOOL:
  case STRING:
  case NIL:
  case BIN:
  case UN:
  case CALL:
  case TYPECONSTRAINT:
  case SIZEOF:
  case ALIGNOF:
  case INIT:
    print_expr(out, mod, node, T__STATEMENT);
    break;
  case BLOCK:
    print_block(out, mod, node);
    break;
  case PHI:
    // noop
    break;
  case JUMP:
    if (node->as.JUMP.is_break) {
      pf(out, "break");
    } else if (node->as.JUMP.is_continue) {
      pf(out, "continue");
    } else {
      pf(out, "goto %s", idents_value(mod->gctx, node->as.JUMP.label));
    }
    break;
  default:
    fprintf(g_env.stderr, "Unsupported node: %d\n", node->which);
    assert(false);
  }
}

static void print_block(struct out *out, const struct module *mod, const struct node *node) {
  assert(node->which == BLOCK);
  const bool value_braces = !typ_equal(node->typ, TBI_VOID) || parent_const(node)->which == IF;
  const bool braces = !node->as.BLOCK.is_scopeless;

  out->node = node;

  if (value_braces) {
    pf(out, "({\n");
  } else if (braces) {
    pf(out, "{\n");
  }

  if (node->as.BLOCK.is_scopeless && !subs_count_atleast(node, 2)) {
    print_statement(out, mod, subs_first_const(node));
  } else {
    FOREACH_SUB_CONST(statement, node) {
      const size_t prev_runcount = out->runcount;
      print_statement(out, mod, statement);
      if (out->runcount != prev_runcount && !out->last_was_semi) {
        pf(out, ";\n");
      }
    }
  }

  if (value_braces) {
    pf(out, "})");
  } else if (braces) {
    pf(out, "}");
  }
}

static void print_typeconstraint(struct out *out, const struct module *mod, const struct node *node) {
  print_expr(out, mod, subs_first_const(node), T__STATEMENT);
}

static void print_defarg(struct out *out, const struct module *mod, const struct node *node,
                         bool return_through_ref, bool dynwrapper) {
  if (return_through_ref) {
    if (DEF(node->typ)->which == DEFINTF) {
      if (typ_is_counted_reference(node->typ)) {
        pf(out, "struct _$Ncdyn_");
      } else {
        pf(out, "struct _$Ndyn_");
      }
      bare_print_typ(out, mod, node->typ);
    } else {
      print_typ(out, mod, node->typ);
    }
  } else {
    print_typ(out, mod, node->typ);
  }

  pf(out, " ");

  const struct node *fun = nparent_const(node, 2);
  const bool is_self = fun->which == DEFMETHOD && prev_const(node) == NULL;

  if (!is_self) {
    if (return_through_ref) {
      pf(out, "*_$Nrtr_");
    } else if (callconv_callees(node->typ)) {
      pf(out, "*%s", dynwrapper ? "" : CALLCONV_CALLEES_PREFIX);
    }
  }

  print_expr(out, mod, subs_first_const(node), T__STATEMENT);
}

static bool print_call_vararg_proto(bool *did_retval_throughref,
                                    struct out *out, const struct module *mod,
                                    const struct node *dfun, size_t n,
                                    const struct node *retval, bool as_dynwrapper) {
  const ssize_t first_vararg = node_fun_first_vararg(dfun);
  if (n != first_vararg) {
    return false;
  }

  // Must print return-through-ref first.
  if (!typ_isa_return_by_copy(retval->typ)) {
    *did_retval_throughref = true;

    print_defarg(out, mod, retval, true, as_dynwrapper);
    pf(out, ", ");
  }

  pf(out, "n$builtins$Int _$Nvacount, ...");
  return true;
}

static void print_fun_prototype(struct out *out, bool header, enum forward fwd,
                                const struct module *mod,
                                const struct node *node,
                                bool as_fun_pointer,
                                bool as_dyn_fun_pointer) {
  const bool as_dynwrapper = !as_fun_pointer && as_dyn_fun_pointer;
  const struct node *proto = node;
  if (as_dyn_fun_pointer && node_member_from_intf(node)) {
    proto = DEF(typ_member(CONST_CAST(node_member_from_intf(node)), node_ident(node)));
  }

  const size_t args_count = c_fun_args_count(proto);
  const struct node *retval = node_fun_retval_const(proto);
  const bool retval_throughref = !typ_isa_return_by_copy(retval->typ);

  if (!as_fun_pointer && !as_dyn_fun_pointer) {
    print_fun_linkage(out, header, fwd, node);
  }

  if (retval_throughref) {
    pf(out, "void");
  } else {
    print_typ(out, mod, retval->typ);
  }

  pf(out, " ");
  if (as_fun_pointer) {
    pf(out, "(*");
    pf(out, "%s", idents_value(mod->gctx, node_ident(node)));
    pf(out, ")");
  } else {
    print_deffun_name(out, mod, node);
    if (as_dyn_fun_pointer) {
      assert(as_dynwrapper);
      pf(out, "__$Ndynwrapper");
    }
  }
  pf(out, "(");

  bool no_args_at_all = true;
  bool force_comma = false;
  bool did_retval_throughref = false;

  const struct node *funargs = subs_at_const(proto, IDX_FUNARGS);
  size_t n;
  for (n = 0; n < args_count; ++n) {
    no_args_at_all = false;
    if (force_comma || n > 0) {
      pf(out, ", ");
    }

    if (print_call_vararg_proto(&did_retval_throughref,
                                out, mod, proto, n, retval, as_dynwrapper)) {
      break;
    }

    const struct node *arg = subs_at_const(funargs, n);

    if (n == 0 && proto->which == DEFMETHOD && as_dyn_fun_pointer) {
      if (typ_is_counted_reference(arg->typ)) {
        pf(out, "struct NLANG_CREF() self");
      } else {
        pf(out, "void *self");
      }
      continue;
    }

    print_defarg(out, mod, arg, false, as_dynwrapper);
  }

  if (retval_throughref && !did_retval_throughref) {
    no_args_at_all = false;
    if (force_comma || n > 0) {
      pf(out, ", ");
    }

    print_defarg(out, mod, retval, true, as_dynwrapper);
  }

  if (no_args_at_all) {
    pf(out, "void");
  }

  pf(out, ")");
}

static void print_rtr_helpers_start(struct out *out, const struct module *mod,
                                    const struct node *retval, bool bycopy) {
  const struct node *first = subs_first_const(retval);
  const struct node *last = subs_last_const(retval);
  if (bycopy) {
    pf(out, UNUSED " ");
    print_defarg(out, mod, retval, false, false);
    pf(out, " = { 0 };\n");
  } else {
    pf(out, "#define ");
    print_expr(out, mod, first, T__STATEMENT);
    pf(out, " (*_$Nrtr_");
    print_expr(out, mod, first, T__STATEMENT);
    pf(out, ")\n");
  }

  if (last->which == TUPLE) {
    size_t n = 0;
    FOREACH_SUB_CONST(x, last) {
      if (x->which == DEFARG) {
        pf(out, "#define ");
        print_expr(out, mod, subs_first_const(x), T__STATEMENT);
        pf(out, " ((");
        print_expr(out, mod, first, T__STATEMENT);
        pf(out, ").X%zu)\n", n);
      }
      n += 1;
    }
  }
}

static void print_rtr_helpers_end(struct out *out, const struct module *mod,
                                  const struct node *retval, bool bycopy) {
  const struct node *first = subs_first_const(retval);
  const struct node *last = subs_last_const(retval);
  if (bycopy) {
    pf(out, "return ");
    print_expr(out, mod, first, T__STATEMENT);
    pf(out, ";\n");
  } else {
    pf(out, "#undef ");
    print_expr(out, mod, first, T__STATEMENT);
    pf(out, "\n");
  }

  if (last->which == TUPLE) {
    FOREACH_SUB_CONST(x, last) {
      if (x->which == DEFARG) {
        pf(out, "#undef ");
        print_expr(out, mod, subs_first_const(x), T__STATEMENT);
        pf(out, "\n");
      }
    }
  }
}

// Possible forms
//   DEFARG
//    IDENT
//    type
//
//  DEFARG
//   TUPLE
//    TYPE
//    TYPE
//
//  TUPLE
//   DEFARG
//    IDENT
//    type
//   DEFARG
//    IDENT
//    type
static void print_rtr_helpers(struct out *out, const struct module *mod,
                              const struct node *retval, bool start) {
  if (typ_equal(retval->typ, TBI_VOID)) {
    return;
  }

  const bool bycopy = typ_isa(retval->typ, TBI_RETURN_BY_COPY);
  if (start) {
    print_rtr_helpers_start(out, mod, retval, bycopy);
  } else {
    print_rtr_helpers_end(out, mod, retval, bycopy);
  }
}

static void rtr_helpers(struct out *out, const struct module *mod,
                        const struct node *node, bool start) {
  const struct node *retval = node_fun_retval_const(node);
  print_rtr_helpers(out, mod, retval, start);
}

static void print_rtr_name(struct out *out, const struct module *mod,
                           const struct node *node) {
  const struct node *retval = node_fun_retval_const(node);
  print_ident_arg_prefix(out, mod, retval);
  pf(out, "%s", idents_value(mod->gctx, node_ident(retval)));
}

static void print_deffun_builtingen(struct out *out, const struct module *mod, const struct node *node) {
  const struct node *par = parent_const(node);

  pf(out, " {\n");
  if (par->which == DEFTYPE || par->which == DEFCHOICE) {
    pf(out, "#define THIS(x) ");
    print_typ(out, mod, par->typ);
    pf(out, "##x\n");
  }

  rtr_helpers(out, mod, node, true);

  const struct typ *rettyp = typ_function_return_const(node->typ);

  const enum builtingen bg = node_toplevel_const(node)->builtingen;
  switch (bg) {
  case BG_TRIVIAL_CTOR_CTOR:
    break;
  case BG_TRIVIAL_DTOR_DTOR:
    break;
  case BG_TRIVIAL_COPY_COPY_CTOR:
    pf(out, "memmove(self, _Narg_other, sizeof(*self));\n");
    break;
  case BG_TRIVIAL_COMPARE_OPERATOR_COMPARE:
    pf(out, "return memcmp(self, _Narg_other, sizeof(*self));\n");
    break;
  case BG_ENUM_FROM_TAG:
    // FIXME(e): validate tag
    assert(par->which == DEFTYPE);
    if (typ_isa(node->typ, TBI_RETURN_BY_COPY)) {
      pf(out, "return (");
    } else {
      pf(out, "_Narg__nretval = (");
    }
    print_typ(out, mod, rettyp);
    pf(out, "){ .X = (");
    if (par->as.DEFTYPE.kind == DEFTYPE_ENUM) {
      pf(out, "_Narg_value");
    } else {
      pf(out, "(THIS()){ .%s = _Narg_value }", idents_value(mod->gctx, ID_TAG));
    }
    pf(out, "), .Nonnil = 1 };\n");
    break;
  case BG_ENUM_TAG:
    assert(par->which == DEFTYPE);
    if (par->as.DEFTYPE.kind == DEFTYPE_ENUM) {
      pf(out, "return *self;\n");
    } else {
      pf(out, "return self->%s;\n", idents_value(mod->gctx, ID_TAG));
    }
    break;
  case BG_SIZEOF:
    // TODO(e): Should we restrict visibility of Sizeof on export/inline?
    if (!node_is_extern(par)) {
      pf(out, "return (n$builtins$optuint){ .X = sizeof(THIS()), .Nonnil = 1 };\n");
    }
    break;
  case BG_ALIGNOF:
    // TODO(e): Should we restrict visibility of Sizeof on export/inline?
    if (!node_is_extern(par)) {
      pf(out, "return (n$builtins$optuint){ .X = __alignof__(THIS()), .Nonnil = 1 };\n");
    }
    break;
  case BG_MOVE:
    print_rtr_name(out, mod, node);
    pf(out, " = *self; memset(self, 0, sizeof(*self));\n");
    break;
  case BG__NOT:
  case BG__NUM:
    assert(false);
  }

  rtr_helpers(out, mod, node, false);

  if (par->which == DEFTYPE || par->which == DEFCHOICE) {
    pf(out, "#undef THIS\n");
  }
  pf(out, "}\n");
}

static bool do_fun_nonnull_attribute(struct out *out, const struct typ *tfun) {
  if (out != NULL) {
    pf(out, " __attribute__((__nonnull__(");
  }
  size_t count = 0;
  for (size_t n = 0, arity = typ_function_arity(tfun); n < arity; ++n) {
    const struct typ *a = typ_function_arg_const(tfun, n);
    if (typ_is_reference(a)
        && !typ_is_counted_reference(a)
        && !typ_isa(a, TBI_ANY_NREF)
        && !typ_is_dyn(a)) {
      if (count > 0) {
        if (out != NULL) {
          pf(out, ", ");
        }
      }
      if (out != NULL) {
        pf(out, "%zu", 1 + n);
      }
      count += 1;
    }
  }
  if (out != NULL) {
    pf(out, ")))\n");
  }

  return count > 0;
}

static void fun_nonnull_attribute(struct out *out, bool header,
                                  const struct module *mod,
                                  const struct node *node) {
  const struct typ *tfun = node->typ;
  const size_t arity = typ_function_arity(tfun);
  if (arity == 0) {
    return;
  }

  if (do_fun_nonnull_attribute(NULL, tfun)) {
    (void) do_fun_nonnull_attribute(out, tfun);
  }
}

static void guard_generic(struct out *out, bool header, enum forward fwd,
                          const struct module *mod,
                          const struct node *node,
                          bool begin) {
  const struct typ *t = node->typ;
  const struct node *par = parent_const(node);

  const char *prefix = "";
  if (node->which == DEFINTF) {
    prefix = "_$Ndyn_";
  } else if (typ_generic_arity(t) == 0 && typ_generic_arity(par->typ) == 0) {
    return;
  }

  if (begin) {
    pf(out, "#ifndef HAS%x_%s", fwd, prefix);
    bare_print_typ_actual(out, mod, t);
    pf(out, "\n#define HAS%x_%s", fwd, prefix);
    bare_print_typ_actual(out, mod, t);
    pf(out, "\n");
  } else {
    pf(out, "#endif // HAS%x_%s", fwd, prefix);
    bare_print_typ_actual(out, mod, t);
    pf(out, "\n");
  }
}

// Method pointer stored in dyntable must be polymorph in self. We cannot
// simply cast the actual function pointer to the same prototype with the
// type of self as void *, as it is not supported by emscripten.
// Instead, we create a wrapper with the same name as the method and the
// suffix __$Ndynwrapper, which is only used in the dyntable.
static void print_deffun_dynwrapper(struct out *out, bool header, enum forward fwd,
                                    const struct module *mod, const struct node *node) {
  if (fwd != FWD_DECLARE_FUNCTIONS && fwd != FWD_DEFINE_FUNCTIONS) {
    return;
  }
  if (node_toplevel_const(node)->flags & TOP_IS_NOT_DYN) {
    return;
  }

  const struct typ *from_intf = node_member_from_intf(node);
  if (from_intf == NULL) {
    return;
  }
  const struct node *from = DEF(typ_member(CONST_CAST(from_intf), node_ident(node)));
  if (node_toplevel_const(from)->flags & TOP_IS_NOT_DYN) {
    return;
  }

  pf(out, WEAK " ");
  print_fun_prototype(out, header, fwd, mod, node, false, true);
  if (fwd == FWD_DECLARE_FUNCTIONS) {
    pf(out, ";\n");
    return;
  }

  const struct node *retval = node_fun_retval_const(node);
  const bool retval_throughref = !typ_isa_return_by_copy(retval->typ);
  const bool from_retval_throughref = !typ_isa_return_by_copy(typ_function_return_const(from->typ));

  pf(out, " {\n");

  const struct node *funargs = subs_at_const(from, IDX_FUNARGS);

  rtr_helpers(out, mod, node, true);

  const ssize_t first_vararg = node_fun_first_vararg(node);
  ident id_ap = ID__NONE;
  if (first_vararg >= 0) {
    const struct node *ap = subs_at_const(funargs, first_vararg);
    id_ap = node_ident(ap);
    print_typ(out, mod, ap->typ);
    pf(out, " %s = { 0 };\nNLANG_BUILTINS_VARARG_START(%s);\n",
            idents_value(mod->gctx, id_ap),
            idents_value(mod->gctx, id_ap));
  }

  if (!typ_equal(retval->typ, TBI_VOID)) {
    if (!from_retval_throughref) {
      pf(out, "return ");
    }
  }
  print_typ(out, mod, node->typ);
  pf(out, "(");

  size_t n = 0;
  FOREACH_SUB_CONST(arg, funargs) {
    if (next_const(arg) == NULL) {
      break;
    }
    bool need_closing_par = false;
    if (n == 0) {
      const struct typ *to_self = typ_function_arg_const(node->typ, 0);
      if (typ_is_counted_reference(to_self)) {
        need_closing_par = true;
        pf(out, "NLANG_CREF_CAST(");
        print_typ(out, mod, to_self);
        pf(out, ", NLANG_CREF_INCR(");
      }
    }
    if (n > 0) {
      pf(out, ", ");
    }
    if (n == typ_function_first_vararg(node->typ)) {
      pf(out, "NLANG_BUILTINS_VACOUNT_VARARGREF, &");
    }

    print_ident(out, mod, subs_first_const(arg));

    if (need_closing_par) {
      pf(out, "))");
    }

    n += 1;
  }

  if (retval_throughref) {
    if (n > 0) {
      pf(out, ", &");
    }
    print_ident(out, mod, subs_first_const(retval));
  }

  pf(out, ");\n");

  if (first_vararg >= 0) {
    pf(out, "NLANG_BUILTINS_VARARG_END(%s);\n",
            idents_value(mod->gctx, id_ap));
  }

  rtr_helpers(out, mod, node, false);

  pf(out, "}\n");
}

static void print_deffun(struct out *out, bool header, enum forward fwd,
                         const struct module *mod, const struct node *node,
                         struct fintypset *printed) {
  out->node = node;

  const struct node *par = parent_const(node);
  if (fwd != FWD_DECLARE_FUNCTIONS && fwd != FWD_DEFINE_FUNCTIONS) {
    return;
  }
  if (par->which == DEFINTF) {
    return;
  }
  if (fun_is_newtype_pretend_wrapper(node)) {
    return;
  }
  if (node_ident(node) == ID_NEXT
      && typ_generic_functor_const(par->typ) != NULL
      && typ_equal(typ_generic_functor_const(par->typ), TBI_VARARG)) {
    // This is a builtin and does not have a real function prototype.
    return;
  }

  const ident id = node_ident(node);
  if (id == ID_CAST
      || id == ID_DYNCAST
      || id == ID_LIKELY
      || id == ID_UNLIKELY) {
    return;
  }

  if (fwd == FWD_DEFINE_FUNCTIONS) {
    if (node_is_extern(node)) {
      print_deffun_dynwrapper(out, header, fwd, mod, node);
      return;
    } else if (node_is_prototype(node)) {
      return;
    }
  }

  if (skip(header, fwd, node)) {
    return;
  }

  guard_generic(out, header, fwd, mod, node, true);

  if (fwd == FWD_DECLARE_FUNCTIONS) {
    if (node->which == DEFFUN && node->as.DEFFUN.example > 0) {
      pf(out, SECTION_EXAMPLES "\n");
    }
    fun_nonnull_attribute(out, header, mod, node);
    print_fun_prototype(out, header, fwd, mod, node, false, false);
    pf(out, ";\n");
  } else if (node_toplevel_const(node)->builtingen != BG__NOT) {
    print_fun_prototype(out, header, fwd, mod, node, false, false);
    print_deffun_builtingen(out, mod, node);
  } else {
    print_fun_prototype(out, header, fwd, mod, node, false, false);

    pf(out, " {\n");
    if (par->which == DEFTYPE) {
      pf(out, "#define THIS(x) ");
      print_typ(out, mod, par->typ);
      pf(out, "##x\n");
    }
    if (node->which == DEFMETHOD
        && (node_ident(node) == ID_DTOR || node_ident(node) == ID_MOVE)) {
      pf(out, "NLANG_CLEANUP_ZERO(self);\n");
    }

    rtr_helpers(out, mod, node, true);

    const struct node *funargs = subs_at_const(node, IDX_FUNARGS);
    FOREACH_SUB_CONST(arg, funargs) {
      if (next_const(arg) == NULL || arg->as.DEFARG.is_vararg) {
        break;
      }
      if (callconv_callees(arg->typ)) {
        const char *narg = idents_value(mod->gctx, node_ident(arg));
        pf(out, "#define _Narg_%s (*" CALLCONV_CALLEES_PREFIX "_Narg_%s)\n", narg, narg);

        print_defer_dtor_callees(out, mod, arg);
      }
    }

    const ssize_t first_vararg = node_fun_first_vararg(node);
    ident id_ap = ID__NONE;
    if (first_vararg >= 0) {
      const struct node *ap = subs_at_const(funargs, first_vararg);
      id_ap = node_ident(ap);
      print_typ(out, mod, ap->typ);
      pf(out, " %s = { 0 };\nNLANG_BUILTINS_VARARG_START(%s);\n",
              idents_value(mod->gctx, id_ap),
              idents_value(mod->gctx, id_ap));
    }

    const struct node *block = subs_last_const(node);
    print_block(out, mod, block);

    pf(out, "\n");

    if (first_vararg >= 0) {
      pf(out, "NLANG_BUILTINS_VARARG_END(%s);\n",
              idents_value(mod->gctx, id_ap));
    }

    if (par->which == DEFTYPE
        && par->as.DEFTYPE.kind == DEFTYPE_UNION) {
      if (node_ident(node) == ID_COPY_CTOR) {
        pf(out, "self->Tag = _Narg_other->Tag;\n");
      } else if (node_ident(node) == ID_MOVE) {
        pf(out, "_Narg_other.Tag = self->Tag;\n");
      }
    }

    FOREACH_SUB_CONST(arg, funargs) {
      if (next_const(arg) == NULL || arg->as.DEFARG.is_vararg) {
        break;
      }
      if (callconv_callees(arg->typ)) {
        const char *narg = idents_value(mod->gctx, node_ident(arg));
        pf(out, "#undef _Narg_%s\n", narg);
      }
    }

    rtr_helpers(out, mod, node, false);

    if (par->which == DEFTYPE) {
      pf(out, "#undef THIS\n");
    }
    pf(out, "}\n");
  }

  print_deffun_dynwrapper(out, header, fwd, mod, node);

  guard_generic(out, header, fwd, mod, node, false);
}

static void print_deffield(struct out *out, const struct module *mod, const struct node *node) {
  print_typ(out, mod, node->typ);
  pf(out, " ");
  print_deffield_name(out, mod, node);
}

static void print_deftype_statement(size_t *count_fields,
                                    struct out *out, bool header, enum forward fwd,
                                    const struct module *mod, const struct node *node,
                                    bool do_static, struct fintypset *printed) {
  out->node = NULL;

  if (do_static) {
    switch (node->which) {
    case INVARIANT:
      if (fwd == FWD_DEFINE_FUNCTIONS) {
        print_invariant(out, mod, node);
      }
      break;
    default:
      break;
    }
  } else {
    switch (node->which) {
    case DEFFIELD:
      if (fwd == FWD_DEFINE_TYPES) {
        print_deffield(out, mod, node);
        *count_fields += 1;
      }
      break;
    default:
      break;
    }
  }
}

struct cprinter_state {
  struct out *out;
  bool header;
  enum forward fwd;
  const struct module *mod;
  size_t printed;
  void *user;
  bool force;
};

static void print_deftype_block(struct out *out, bool header, enum forward fwd, const struct module *mod,
                                const struct node *node, bool do_static, struct fintypset *printed) {
  assert(node->which == DEFTYPE && node->as.DEFTYPE.kind == DEFTYPE_STRUCT);

  if (!do_static) {
    pf(out, " {\n");
  }

  size_t count_fields = 0;
  FOREACH_SUB_EVERY_CONST(statement, node, 2, 1) {
    const size_t prev_runcount = out->runcount;
    print_deftype_statement(&count_fields, out, header, fwd, mod, statement, do_static, printed);
    if (out->runcount != prev_runcount && !out->last_was_semi) {
      pf(out, ";\n");
    }
  }

  if (!do_static) {
    if (fwd == FWD_DEFINE_TYPES && count_fields == 0) {
      pf(out, "n$builtins$U8 _$Nfiller;\n");
    }
    pf(out, "};\n");
  }
}

static ERROR print_defintf_dyntable_field_eachisalist(struct module *mod, struct typ *t,
                                                      struct typ *intf,
                                                      bool *stop, void *user) {
  struct cprinter_state *st = user;

  const struct node *dintf = DEF(intf);
  FOREACH_SUB_CONST(d, dintf) {
    if (d->which != DEFFUN && d->which != DEFMETHOD) {
      continue;
    }
    if (node_toplevel_const(d)->flags & TOP_IS_NOT_DYN) {
      continue;
    }
    if (subs_count_atleast(subs_at_const(DEF(d->typ), IDX_GENARGS), 1)) {
      continue;
    }
    print_fun_prototype(st->out, st->header, st->fwd, mod, d, true, true);
    pf(st->out, ";\n");
    st->printed += 1;
  }

  return 0;
}

static void print_dyntable_type(struct out *out, bool header, enum forward fwd,
                                const struct module *mod, const struct node *node) {
  if (fwd == FWD_DECLARE_TYPES) {
    pf(out, "struct _$Ndyntable_");
    bare_print_typ(out, mod, node->typ);
    pf(out, ";\n");

    if (node->which == DEFINTF) {
      pf(out, "struct _$Ndyn_");
      print_deftype_name(out, mod, node);
      pf(out, ";\n");
    }

  } else if (fwd == FWD_DEFINE_DYNS) {
    pf(out, "struct _$Ndyntable_");
    bare_print_typ(out, mod, node->typ);
    pf(out, " {\n");

    // Must be first: see lib/n/reflect.n.h
    pf(out, "const void *type;\n");

    struct cprinter_state st = {
      .out = out, .header = header, .fwd = fwd,
      .mod = NULL, .printed = 0, .user = NULL, .force = false,
    };

    if (node->which == DEFINTF) {
      error e = typ_isalist_foreach(CONST_CAST(mod), node->typ,
                                    ISALIST_FILTEROUT_PREVENT_DYN,
                                    // filter is 0 because all members of an
                                    // intf inherit the exported status from
                                    // the intf itself.
                                    print_defintf_dyntable_field_eachisalist,
                                    &st);
      assert(!e);
      if (!typ_equal(node->typ, TBI_ANY)) {
        // To prevent double-printing.
        e = print_defintf_dyntable_field_eachisalist(CONST_CAST(mod), node->typ,
                                                     node->typ, NULL, &st);
        assert(!e);
      }
    }

    if (st.printed == 0) {
      // Needed if all the members of the intf are *themselves* generics,
      // that form is indeed legal. Or if we printing the empty dyntable
      // of a DEFTYPE.
      pf(out, "n$builtins$U8 _$Nfiller;\n");
    }
    pf(out, "};\n");

    if (node->which == DEFINTF) {
      pf(out, "struct _$Ndyn_");
      print_deftype_name(out, mod, node);
      pf(out, " {\n");
      // 'ref' comes first so that a dyn can be brutally cast to the
      // underlying pointer.
      pf(out, "void *ref;\n");
      pf(out, "const struct _$Ndyntable_");
      bare_print_typ(out, mod, node->typ);
      pf(out, " *dyntable;\n");
      pf(out, "};\n");

      pf(out, "struct _$Ncdyn_");
      print_deftype_name(out, mod, node);
      pf(out, " {\n");
      // ref' comes first so that a cdyn can be brutally cast to the
      // underlying Cref.
      pf(out, "struct NLANG_CREF() ref;\n");
      pf(out, "const struct _$Ndyntable_");
      bare_print_typ(out, mod, node->typ);
      pf(out, " *dyntable;\n");
      pf(out, "};\n");
    }
  }
}

static ERROR print_dyntable_proto_eachisalist(struct module *mod, struct typ *t,
                                              struct typ *intf,
                                              bool *stop, void *user) {
  struct cprinter_state *st = user;
  if (typ_is_reference(intf)) {
    return 0;
  }

  pf(st->out, WEAK "extern const struct _$Ndyntable_");
  bare_print_typ(st->out, mod, intf);
  pf(st->out, " ");
  bare_print_typ_actual(st->out, mod, t);
  pf(st->out, "$Dyntable__");
  bare_print_typ_actual(st->out, mod, intf);
  pf(st->out, ";\n");
  return 0;
}

static ERROR print_dyntable_field_eachisalist(struct module *mod, struct typ *ignored,
                                              struct typ *intf,
                                              bool *stop, void *user) {
  struct cprinter_state *st = user;
  struct typ *t = st->user;
  if (typ_is_reference(intf)) {
    return 0;
  }

  const struct node *dintf = DEF(intf);
  FOREACH_SUB_CONST(f, dintf) {
    if (f->which != DEFFUN && f->which != DEFMETHOD) {
      continue;
    }
    if (node_toplevel_const(f)->flags & TOP_IS_NOT_DYN) {
      continue;
    }
    if (subs_count_atleast(subs_at_const(DEF(f->typ), IDX_GENARGS), 1)) {
      continue;
    }

    st->printed += 1;
    const struct node *thisf = node_get_member_const(DEF(t),
                                                     node_ident(f));
    if (!typ_is_concrete(thisf->typ)) {
      return 0;
    }

    pf(st->out, ".%s = ", idents_value(mod->gctx, node_ident(thisf)));
    print_typ(st->out, mod, thisf->typ);
    pf(st->out, "%s,\n", f->which == DEFMETHOD ? "__$Ndynwrapper" : "");
  }

  return 0;
}

static ERROR print_dyntable_eachisalist(struct module *mod, struct typ *t,
                                        struct typ *intf,
                                        bool *stop, void *user) {
  struct cprinter_state *st = user;
  if (typ_is_reference(intf)) {
    return 0;
  }

  pf(st->out, WEAK "const struct _$Ndyntable_");
  bare_print_typ(st->out, mod, intf);
  pf(st->out, " ");
  bare_print_typ_actual(st->out, mod, t);
  pf(st->out, "$Dyntable__");
  bare_print_typ_actual(st->out, mod, intf);
  pf(st->out, " = {\n");
  pf(st->out, ".type = &");
  bare_print_typ_actual(st->out, mod, t);
  pf(st->out, "$Reflect_type,\n");

  struct cprinter_state st2 = *st;
  st2.printed = 0;
  st2.user = (void *)t;

  if (typ_definition_which(intf) == DEFTYPE) {
    goto skip;
  }

  const uint32_t filter = ISALIST_FILTEROUT_PREVENT_DYN;
  error e = typ_isalist_foreach(CONST_CAST(mod), intf, filter,
                                print_dyntable_field_eachisalist,
                                &st2);
  assert(!e);
  e = print_dyntable_field_eachisalist(CONST_CAST(mod),
                                       NULL, intf, NULL, &st2);
  assert(!e);

skip:
  if (st2.printed == 0) {
    pf(st->out, "0,\n");
  }
  pf(st->out, "};\n");
  return 0;
}

static void print_dyntable(struct out *out, bool header, enum forward fwd,
                           const struct module *mod, const struct node *node) {
  const uint32_t filter = ISALIST_FILTEROUT_PREVENT_DYN;
  if (fwd == FWD_DECLARE_FUNCTIONS) {
    struct cprinter_state st = { .out = out, .header = header, .fwd = fwd,
      .mod = NULL, .printed = 0, .user = NULL, .force = false, };
    error e = print_dyntable_proto_eachisalist(CONST_CAST(mod), node->typ, node->typ, NULL, &st);
    assert(!e);
    e = typ_isalist_foreach(CONST_CAST(mod), node->typ, filter,
                            print_dyntable_proto_eachisalist,
                            &st);
    assert(!e);
  } else if ((!header || typ_generic_arity(node->typ) > 0) && fwd == FWD_DEFINE_FUNCTIONS) {
    struct cprinter_state st = { .out = out, .header = header, .fwd = fwd,
      .mod = NULL, .printed = 0, .user = NULL, .force = false, };
    error e = print_dyntable_eachisalist(CONST_CAST(mod), node->typ, node->typ, NULL, &st);
    assert(!e);
    e = typ_isalist_foreach(CONST_CAST(mod), node->typ, filter,
                            print_dyntable_eachisalist,
                            &st);
    assert(!e);
  }
}

static void print_reflect_type(struct out *out, bool header, enum forward fwd,
                               const struct module *mod, const struct node *node) {
  if (typ_is_reference(node->typ)) {
    return;
  }

  if (fwd == FWD_DECLARE_TYPES) {
    pf(out, WEAK "extern const struct __Type ");
    bare_print_typ_actual(out, mod, node->typ);
    pf(out, "$Reflect_type;\n");
    return;
  } else if ((header && typ_generic_arity(node->typ) == 0) || fwd != FWD_DEFINE_FUNCTIONS) {
    return;
  }

  if (node->which == DEFINTF) {
    struct __Type *type = node->as.DEFINTF.reflect_type;
    pf(out, WEAK " const struct __Type ");
    bare_print_typ_actual(out, mod, node->typ);
    pf(out, "$Reflect_type = {\n");
    pf(out, ".typename_hash32 = 0x%x,\n", type->typename_hash32);
    pf(out, ".Typename = NLANG_STRING_LITERAL(\"%.*s\"),\n",
            (int)type->Typename.bytes.cnt, type->Typename.bytes.dat);
    pf(out, "0 };\n");
    return;
  }

  struct __Type *type = node->as.DEFTYPE.reflect_type;
  pf(out, "static uint16_t ");
  bare_print_typ_actual(out, mod, node->typ);
  pf(out, "$Reflect_type__hashmap[] = {\n");
  for (size_t n = 0; n < type->dynisalist.hashmap.cnt; n += 10) {
    for (size_t i = n; i < n + 10 && i < type->dynisalist.hashmap.cnt; ++i) {
      pf(out, "0x%hx,", type->dynisalist.hashmap.dat[i]);
    }
    pf(out, "\n");
  }
  pf(out, "};\n");

  pf(out, "static struct __entry ");
  bare_print_typ_actual(out, mod, node->typ);
  pf(out, "$Reflect_type__entries[] = {\n");
  for (size_t n = 0; n < type->dynisalist.entries.cnt; ++n) {
    struct __entry *e = &type->dynisalist.entries.dat[n];
    pf(out, "{ .typename_hash32 = 0x%x, .Typename = NLANG_STRING_LITERAL(\"%.*s\"), ",
            e->typename_hash32, (int)e->Typename.bytes.cnt, e->Typename.bytes.dat);

    const struct typ *intf = e->dyntable;
    pf(out, "&");
    bare_print_typ_actual(out, mod, node->typ);
    pf(out, "$Dyntable__");
    bare_print_typ_actual(out, mod, intf);
    pf(out, " },\n");
  }
  pf(out, "};\n");

  pf(out, WEAK " const struct __Type ");
  bare_print_typ_actual(out, mod, node->typ);
  pf(out, "$Reflect_type = {\n");
  pf(out, ".typename_hash32 = 0x%x,\n", type->typename_hash32);
  pf(out, ".Typename = NLANG_STRING_LITERAL(\"%.*s\"),\n",
          (int)type->Typename.bytes.cnt, type->Typename.bytes.dat);
  pf(out, ".dynisalist = {\n");

  pf(out, ".hashmap = {\n");
  pf(out, ".dat = ");
  bare_print_typ_actual(out, mod, node->typ);
  pf(out, "$Reflect_type__hashmap,\n");
  pf(out, ".cnt = %zu,\n.cap = %zu,\n},",
          type->dynisalist.hashmap.cnt, type->dynisalist.hashmap.cap);

  pf(out, ".entries = {\n");
  pf(out, ".dat = ");
  bare_print_typ_actual(out, mod, node->typ);
  pf(out, "$Reflect_type__entries,\n");
  pf(out, ".cnt = %zu,\n.cap = %zu,\n},",
          type->dynisalist.entries.cnt, type->dynisalist.entries.cap);

  pf(out, "},\n};\n");
}

static void print_defchoice_payload(struct out *out,
                                    enum forward fwd,
                                    const struct module *mod,
                                    const struct node *deft,
                                    const struct node *ch) {
  if (ch->which == DEFCHOICE) {
    const struct node *ext = node_defchoice_external_payload(ch);
    if (ext != NULL) {
      if (fwd == FWD_DECLARE_TYPES) {
        forward_declare(out, mod, ext->typ);
        pf(out, "#ifndef ");
        print_defchoice_path(out, mod, deft, ch);
        pf(out, "\n#define ");
        print_defchoice_path(out, mod, deft, ch);
        pf(out, " ");
        print_typ(out, mod, ext->typ);
        pf(out, "\n#endif\n");
      }
      return;
    }
  }

  pf(out, "struct ");
  print_defchoice_path(out, mod, deft, ch);

  if (fwd == FWD_DECLARE_TYPES) {
    pf(out, ";\n");
    return;
  }

  pf(out, " {\n");

  if (!subs_count_atleast(ch, IDX_CH_FIRST_PAYLOAD+1)) {
    print_typ(out, mod, TBI_U8);
    pf(out, " _$Nfiller;\n};\n");
    return;
  }

  FOREACH_SUB_EVERY_CONST(m, ch, IDX_CH_FIRST_PAYLOAD, 1) {
    if (m->which == DEFFIELD) {
      print_deffield(out, mod, m);
      pf(out, ";\n");
    }
  }

  if (ch == deft) {
    print_defchoice_path(out, mod, deft, deft);
    pf(out, "$%s %s;\n",
            idents_value(mod->gctx, ID_TAG_TYPE),
            idents_value(mod->gctx, ID_TAG));
  }

  if (!ch->as.DEFCHOICE.is_leaf) {
    pf(out, "union ");
    print_defchoice_path(out, mod, deft, ch);
    pf(out, "$%s %s;\n",
            idents_value(mod->gctx, ID_AS_TYPE),
            idents_value(mod->gctx, ID_AS));
  }

  pf(out, "};\n");
}

static void print_defchoice_leaf(struct out *out,
                                 const struct module *mod,
                                 const struct node *deft,
                                 const struct node *ch) {
  pf(out, "#define ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_TAG));
  print_expr(out, mod, subs_at_const(ch, IDX_CH_TAG_FIRST), T__NOT_STATEMENT);
  pf(out, ")\n");

  pf(out, UNUSED "static ");
  print_deftype_name(out, mod, deft);
  pf(out, "$%s", idents_value(mod->gctx, ID_TAG_TYPE));
  pf(out, " ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s = ", idents_value(mod->gctx, ID_TAG));
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_TAG));
}

static void print_defchoice(struct out *out,
                            const struct module *mod,
                            const struct node *deft,
                            const struct node *ch) {
  if (ch->as.DEFCHOICE.is_leaf) {
    print_defchoice_leaf(out, mod, deft, ch);
    return;
  }

  pf(out, "#define ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_FIRST_TAG));
  print_expr(out, mod, subs_at_const(ch, IDX_CH_TAG_FIRST), T__NOT_STATEMENT);
  pf(out, ")\n");

  pf(out, UNUSED "static ");
  print_deftype_name(out, mod, deft);
  pf(out, "$%s", idents_value(mod->gctx, ID_TAG_TYPE));
  pf(out, " ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s = ", idents_value(mod->gctx, ID_FIRST_TAG));
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_FIRST_TAG));

  pf(out, "#define ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_LAST_TAG));
  print_expr(out, mod, subs_at_const(ch, IDX_CH_TAG_FIRST), T__NOT_STATEMENT);
  pf(out, ")\n");

  pf(out, UNUSED "static ");
  print_deftype_name(out, mod, deft);
  pf(out, "$%s", idents_value(mod->gctx, ID_TAG_TYPE));
  pf(out, " ");
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s = ", idents_value(mod->gctx, ID_LAST_TAG));
  print_defchoice_path(out, mod, deft, ch);
  pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_LAST_TAG));
}

static void print_enumunion_functions(struct out *out, bool header, enum forward fwd,
                                      const struct module *mod,
                                      const struct node *deft,
                                      const struct node *ch,
                                      struct fintypset *printed) {
  FOREACH_SUB_CONST(m, ch) {
    switch (m->which) {
    case DEFFUN:
    case DEFMETHOD:
      print_top(out, header, fwd, mod, m, printed, false);
      break;
    case DEFCHOICE:
      print_enumunion_functions(out, header, fwd, mod, deft, m, printed);
      break;
    default:
      break;
    }
  }
}

static void print_union_types(struct out *out, bool header, enum forward fwd,
                              const struct module *mod, const struct node *deft,
                              const struct node *ch) {
  if (fwd == FWD_DECLARE_TYPES) {
    if (ch != deft) {
      print_defchoice(out, mod, deft, ch);
    }
  }

  if (ch->which != DEFCHOICE || !ch->as.DEFCHOICE.is_leaf) {
    pf(out, "union ");
    print_defchoice_path(out, mod, deft, ch);
    pf(out, "$%s" , idents_value(mod->gctx, ID_AS_TYPE));
    if (fwd == FWD_DEFINE_TYPES) {
      pf(out, " {\n");
      FOREACH_SUB_CONST(m, ch) {
        if (m->which == DEFCHOICE) {
          if (node_defchoice_external_payload(m) == NULL) {
            pf(out, "struct ");
          }
          print_defchoice_path(out, mod, deft, m);
          pf(out, " %s;\n",
                  idents_value(mod->gctx, node_ident(m)));
        }
      }
      pf(out, "}");
    }
    pf(out, ";\n");
  }

  print_defchoice_payload(out, fwd, mod, deft, ch);
}

static void print_union(struct out *out, bool header, enum forward fwd,
                        const struct module *mod, const struct node *deft,
                        const struct node *node, struct fintypset *printed) {
  out->node = NULL;

  switch (fwd) {
  case FWD_DECLARE_TYPES:
    if (node == deft) {
      forward_declare(out, mod, deft->as.DEFTYPE.tag_typ);
      pf(out, "#ifndef ");
      print_deftype_name(out, mod, deft);
      pf(out, "$%s" , idents_value(mod->gctx, ID_TAG_TYPE));
      pf(out, "\n#define ");
      print_deftype_name(out, mod, deft);
      pf(out, "$%s" , idents_value(mod->gctx, ID_TAG_TYPE));
      pf(out, " ");
      print_typ(out, mod, deft->as.DEFTYPE.tag_typ);
      pf(out, "\n#endif\n");

      print_reflect_type(out, header, fwd, mod, deft);
    }

    // fallthrough
  case FWD_DEFINE_TYPES:
    FOREACH_SUB_CONST(s, node) {
      if (s->which == LET) {
        print_defname(out, header, fwd, mod, subs_first_const(s));
        pf(out, ";\n");
      }
      if (s->which == DEFCHOICE) {
        print_union(out, header, fwd, mod, deft, s, printed);
      }
    }
    print_union_types(out, header, fwd, mod, deft, node);
    break;
  case FWD_DECLARE_FUNCTIONS:
  case FWD_DEFINE_FUNCTIONS:
    FOREACH_SUB_CONST(s, node) {
      if (s->which == LET) {
        print_defname(out, header, fwd, mod, subs_first_const(s));
        pf(out, ";\n");
      }
      if (s->which == DEFCHOICE) {
        print_union(out, header, fwd, mod, deft, s, printed);
      }
    }

    if (node == deft) {
      print_enumunion_functions(out, header, fwd, mod, deft, node, printed);
      print_reflect_type(out, header, fwd, mod, deft);
      print_dyntable(out, header, fwd, mod, deft);
    }
    break;
  default:
    assert(false);
    break;
  }
}

static void print_enum(struct out *out, bool header, enum forward fwd,
                       const struct module *mod, const struct node *deft,
                       const struct node *node, struct fintypset *printed) {
  out->node = NULL;

  if (fwd == FWD_DECLARE_TYPES) {
    if (deft == node) {
      pf(out, "#ifndef ");
      print_deftype_name(out, mod, node);
      pf(out, "\n#define ");
      print_deftype_name(out, mod, node);
      pf(out, " ");
      print_typ(out, mod, node->as.DEFTYPE.tag_typ);
      pf(out, "\n#endif\n");

      print_reflect_type(out, header, fwd, mod, node);
    }
  }

  if (fwd == FWD_DEFINE_TYPES) {
    FOREACH_SUB_CONST(s, node) {
      if (s->which != DEFCHOICE) {
        continue;
      }

      if (s->as.DEFCHOICE.is_leaf) {
        pf(out, "#define ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_TAG));
        print_expr(out, mod, subs_at_const(s, IDX_CH_TAG_FIRST), T__NOT_STATEMENT);
        pf(out, ")\n");

        pf(out, UNUSED "static ");
        print_deftype_name(out, mod, node);
        pf(out, " ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, " = ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_TAG));
      } else {
        pf(out, "#define ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_FIRST_TAG));
        print_expr(out, mod, subs_at_const(s, IDX_CH_TAG_FIRST), T__NOT_STATEMENT);
        pf(out, ")\n");

        pf(out, UNUSED "static ");
        print_deftype_name(out, mod, node);
        pf(out, " ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s = ", idents_value(mod->gctx, ID_FIRST_TAG));
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_FIRST_TAG));

        pf(out, "#define ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__ (", idents_value(mod->gctx, ID_LAST_TAG));
        print_expr(out, mod, subs_at_const(s, IDX_CH_TAG_LAST), T__NOT_STATEMENT);
        pf(out, ")\n");

        pf(out, UNUSED "static ");
        print_deftype_name(out, mod, node);
        pf(out, " ");
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s = ", idents_value(mod->gctx, ID_LAST_TAG));
        print_defchoice_path(out, mod, deft, s);
        pf(out, "$%s_label__;\n", idents_value(mod->gctx, ID_LAST_TAG));

        print_enum(out, header, fwd, mod, deft, s, printed);
      }
    }
  }

  if (fwd == FWD_DECLARE_FUNCTIONS || fwd == FWD_DEFINE_FUNCTIONS) {
    if (deft == node) {
      print_enumunion_functions(out, header, fwd, mod, deft, node, printed);
      print_reflect_type(out, header, fwd, mod, node);
      print_dyntable(out, header, fwd, mod, node);
    }
  }
}

static void print_deftype_reference(struct out *out, bool header, enum forward fwd,
                                    const struct module *mod,
                                    const struct node *node) {
  const struct node *d = DEF(typ_generic_arg_const(node->typ, 0));

  if (d->which == DEFINTF) {
    if (typ_generic_arity(d->typ) > 0) {
      print_defintf(out, header, fwd, mod, node);
    }
    return;
  }

  if (fwd != FWD_DECLARE_TYPES) {
    return;
  }

  guard_generic(out, header, fwd, mod, node, true);

  const char *prefix = "";
  if (typ_is_dyn(d->typ)) {
    prefix = "struct ";
    const struct node *dd = DEF(typ_generic_arg_const(d->typ, 0));
    if (typ_is_counted_reference(d->typ)) {
      pf(out, "struct _$Ncdyn_");
    } else {
      pf(out, "struct _$Ndyn_");
    }
    print_deftype_name(out, mod, dd);
    pf(out, ";\n");
  } else if (d->which == DEFTYPE && d->as.DEFTYPE.kind == DEFTYPE_ENUM) {
    print_enum(out, false, FWD_DECLARE_TYPES, mod, d, d, NULL);
  } else if (d->as.DEFTYPE.newtype_expr != NULL) {
    pf(out, "#ifndef ");
    print_deftype_name(out, mod, d);
    pf(out, "\n#define ");
    print_deftype_name(out, mod, d);
    pf(out, " ");
    print_typ(out, mod, d->as.DEFTYPE.newtype_expr->typ);
    pf(out, "\n#endif\n");
  } else if (!(typ_is_builtin(mod, d->typ) && node_is_extern(d))) {
    prefix = "struct ";
    pf(out, "struct ");
    print_deftype_name(out, mod, d);
    pf(out, ";\n");
  }

  pf(out, "#ifndef _$Nref_");
  print_deftype_name(out, mod, d);
  pf(out, "\n#define _$Nref_");
  print_deftype_name(out, mod, d);
  pf(out, " %s", prefix);
  print_deftype_name(out, mod, d);
  pf(out, "*\n#endif\n");

  guard_generic(out, header, fwd, mod, node, false);
}

static void print_deftype(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node,
                          struct fintypset *printed) {
  if (typ_is_pseudo_builtin(node->typ)) {
    return;
  }

  if ((typ_is_counted_reference(node->typ) && !typ_is_dyn(node->typ))
      || typ_has_same_generic_functor(mod, node->typ, TBI_CREF_IMPL)) {
    if (fwd == FWD_DECLARE_TYPES) {
      guard_generic(out, header, fwd, mod, node, true);
      pf(out, "struct ");
      bare_print_typ(out, mod, node->typ);

      pf(out, " {\n");
      print_typ(out, mod, typ_generic_arg(node->typ, 0));
      pf(out, " *ref;\nn$builtins$U32 *cnt;\n};\n");
      guard_generic(out, header, fwd, mod, node, false);
    }
    return;
  }

  if (typ_is_reference(node->typ)) {
    print_deftype_reference(out, header, fwd, mod, node);
    return;
  }


  if ((fwd != FWD_DECLARE_FUNCTIONS && fwd != FWD_DEFINE_FUNCTIONS)
      && node->as.DEFTYPE.newtype_expr != NULL) {
    return;
  }

  if (node_is_extern(node) && fwd == FWD_DEFINE_TYPES) {
    return;
  }

  if (skip(header, fwd, node)) {
    return;
  }

  guard_generic(out, header, fwd, mod, node, true);

  print_dyntable_type(out, header, fwd, mod, node);

  if (fwd == FWD_DEFINE_DYNS) {
    goto done;
  }

  if (node->as.DEFTYPE.kind == DEFTYPE_ENUM) {
    print_enum(out, header, fwd, mod, node, node, printed);
    goto done;
  } else if (node->as.DEFTYPE.kind == DEFTYPE_UNION) {
    print_union(out, header, fwd, mod, node, node, printed);
    goto done;
  }

  if (fwd == FWD_DECLARE_TYPES) {
    if (typ_is_builtin(mod, node->typ) && node_is_extern(node) && node_is_inline(node)) {
      // noop
    } else {
      pf(out, "struct ");
      print_deftype_name(out, mod, node);
      pf(out, ";\n");

      print_deftype_block(out, header, fwd, mod, node, true, printed);
    }

  } else if (fwd == FWD_DEFINE_TYPES) {
    if (typ_is_builtin(mod, node->typ) && node_is_extern(node) && node_is_inline(node)) {
      // noop
    } else {
      print_deftype_block(out, header, fwd, mod, node, true, printed);

      pf(out, "struct ");
      print_deftype_name(out, mod, node);
      print_deftype_block(out, header, fwd, mod, node, false, printed);
    }
  } else if (fwd == FWD_DECLARE_FUNCTIONS || fwd == FWD_DEFINE_FUNCTIONS) {
    print_deftype_block(out, header, fwd, mod, node, true, printed);
  }

  print_reflect_type(out, header, fwd, mod, node);
  print_dyntable(out, header, fwd, mod, node);

done:
  guard_generic(out, header, fwd, mod, node, false);
}

static void print_defintf(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node) {
  if (typ_is_generic_functor(node->typ)) {
    return;
  }
  const struct node *isalist = subs_at_const(node, IDX_ISALIST);
  if (subs_count_atleast(isalist, 1)
      && typ_equal(subs_first_const(isalist)->typ, TBI_PREVENT_DYN)) {
    return;
  }

  if (typ_is_pseudo_builtin(node->typ)) {
    return;
  }

  if (typ_is_reference(node->typ)) {
    return;
  }

  if (skip(header, fwd, node)) {
    return;
  }

  if (header && typ_generic_arity(node->typ) == 0
      && fwd == FWD_DEFINE_FUNCTIONS) {
    return;
  }

  guard_generic(out, header, fwd, mod, node, true);

  print_dyntable_type(out, header, fwd, mod, node);
  print_reflect_type(out, header, fwd, mod, node);

  guard_generic(out, header, fwd, mod, node, false);
}

static void print_include(struct out *out, const char *filename, const char *postfix) {
  pf(out, "# include \"%s%s\"\n", filename, postfix);
}

static bool file_exists(const char *base, const char *postfix) {
  char *fn = calloc(strlen(base) + strlen(postfix) + 1, sizeof(char));
  strcpy(fn, base);
  strcpy(fn + strlen(base), postfix);
  FILE *f = fopen(fn, "r");
  const bool r = f != NULL;
  if (f != NULL) {
    fclose(f);
  }
  free(fn);
  return r;
}

static void print_import(struct out *out, bool header, enum forward fwd,
                         const struct module *mod, const struct node *node,
                         bool non_inline_deps) {
  struct node *target = NULL;
  error e = scope_lookup(&target, mod, &mod->gctx->modules_root,
                         subs_first_const(node), false);
  assert(!e);
  if (target->which != MODULE) {
    return;
  }

  print_include(out, target->as.MODULE.mod->filename, ".o.h");
}

static uint32_t track_id(bool header, enum forward fwd,
                         bool struct_body_written) {
  return (!!struct_body_written << 9) | (!!header << 8) | (uint8_t)fwd;
}

static bool is_printed(struct fintypset *printed,
                       bool header, enum forward fwd,
                       const struct typ *t,
                       uint32_t topdep_mask) {
  if (typ_equal(t, TBI__NOT_TYPEABLE)) {
    return false;
  }

  uint32_t *at = fintypset_get(printed, t);

  if (at != NULL) {
    const uint32_t at_fwd = (*at) & 0xff;
    const uint32_t at_header = ((*at) >> 8) & 0x1;
    const uint32_t at_struct_body_written = ((*at) >> 9) & 0x1;

    if (fwd == FWD_DEFINE_TYPES
        && (topdep_mask & TOP__TOPDEP_INLINE_STRUCT)
        && !at_struct_body_written) {
      return false;
    }

    if ((!at_header && header) || at_fwd >= fwd) {
      return true;
    }
  }

  return false;
}

static void track_printed(const struct module *mod,
                          struct fintypset *printed,
                          bool header, enum forward fwd,
                          const struct typ *t,
                          bool struct_body_written) {
  if (typ_equal(t, TBI__NOT_TYPEABLE)) {
    return;
  }

  const uint32_t update = track_id(header, fwd, struct_body_written);
  uint32_t *at = fintypset_get(printed, t);
  if (at != NULL) {
    *at = update;
  } else {
    fintypset_set(printed, t, update);
  }
}

static ERROR print_topdeps_each(struct module *mod, struct node *node,
                                struct typ *_t, uint32_t topdep_mask, void *user) {
  struct cprinter_state *st = user;
  struct fintypset *printed = st->user;
  if (topdep_mask & (TOP_IS_FUNCTOR | TOP_IS_PREVENT_DYN)) {
    return 0;
  }

  if (typ_was_zeroed(_t)
      || (typ_is_reference(_t) && DEF(_t)->which == DEFINTF)
      || typ_is_generic_functor(_t)
      || (typ_generic_arity(_t) == 0 && !is_in_topmost_module_typ(_t)
          && (typ_toplevel_flags(_t) & TOP_IS_EXPORT))
      || typ_is_tentative(_t)) {
    return 0;
  }

  const struct typ *t = intercept_slices(st->mod, _t);
  if (st->header
      && !st->force
      && ((typ_is_function(t) && !(topdep_mask & TOP_IS_INLINE))
          || (topdep_mask & (TOP_IS_INLINE | TOP_IS_EXPORT)) == 0)
      && !(!is_in_topmost_module_typ(node->typ) && (topdep_mask & TOP__TOPDEP_INLINE_STRUCT))) {
    return 0;
  }

  if (is_printed(printed, st->header, st->fwd, t, topdep_mask)) {
    return 0;
  }

  const struct node *d = DEF(t);
  const struct module *dmod = node_module_owner_const(d);
  print_top(st->out, st->header, st->fwd, dmod, d, printed,
            st->force || (topdep_mask & TOP__TOPDEP_INLINE_STRUCT));

  return 0;
}

static void print_topdeps(struct out *out, bool header, enum forward fwd,
                          const struct module *mod, const struct node *node,
                          struct fintypset *printed, bool force) {
  if ((!typ_is_concrete(node->typ) && node->which != DEFINTF)
      || (!node_is_at_top(node) && !typ_is_concrete(parent_const(node)->typ))) {
    return;
  }

  const struct toplevel *toplevel = node_toplevel_const(node);
  if (!force && header && !(toplevel->flags & (TOP_IS_EXPORT | TOP_IS_INLINE))) {
    return;
  }

  struct cprinter_state st = {
    .out = out,
    .header = header,
    .fwd = fwd,
    .mod = mod,
    .printed = 0,
    .user = printed,
    .force = force,
  };

  error e = topdeps_foreach(CONST_CAST(mod), CONST_CAST(node),
                            print_topdeps_each, &st);
  assert(!e);
}

static void print_top(struct out *out, bool header, enum forward fwd,
                      const struct module *mod, const struct node *node,
                      struct fintypset *printed, bool force) {
  out->mod = mod;
  out->node = NULL;

  if ((!typ_is_concrete(node->typ) && node->which != DEFINTF)
      || (!node_is_at_top(node) && !typ_is_concrete(parent_const(node)->typ))) {
    return;
  }

  if (NM(node->which) & (NM(NOOP) | NM(DEFINCOMPLETE) | NM(WITHIN))) {
    return;
  }

  if (node->which == LET) {
    // not tracked
  } else {
    if (!force && is_printed(printed, header, fwd, node->typ, 0)) {
      return;
    }
    track_printed(mod, printed, header, fwd, node->typ, false);
  }

  if (!node_is_at_top(node)
      && node_ident(parent_const(node)) == node_ident(DEF(TBI_SLICE))) {
    return;
  }

#if 0
  pf(out, "/*\n");
    stderr=out->c;
    debug_print_topdeps_td(mod, node);
  pf(out, "\n*/\n");
#endif

  static __thread size_t prevent_infinite;
  assert(++prevent_infinite < 1000 && "FIXME When force==true, see t00/fixme10");

  //print_topdeps(out, header, fwd, mod, node, printed, force);

  switch (node->which) {
  case DEFMETHOD:
  case DEFFUN:
    print_deffun(out, header, fwd, mod, node, printed);
    break;
  case DEFTYPE:
    if (fwd != FWD_DEFINE_TYPES ||
        !is_printed(printed, header, fwd, node->typ, TOP__TOPDEP_INLINE_STRUCT)) {
      print_deftype(out, header, fwd, mod, node, printed);
      if (fwd == FWD_DEFINE_TYPES) {
        track_printed(mod, printed, header, fwd, node->typ, true);
      }
    }
    break;
  case DEFINTF:
    print_defintf(out, header, fwd, mod, node);
    break;
  case LET:
    if (fwd != FWD_DEFINE_TYPES
        || !is_printed(printed, header, fwd, node->typ, TOP__TOPDEP_INLINE_STRUCT)) {
      print_defname(out, header, fwd, mod, subs_first_const(node));
      pf(out, ";\n");
      if (fwd == FWD_DEFINE_TYPES) {
        track_printed(mod, printed, header, fwd, node->typ, true);
      }
    }
    break;
  case IMPORT:
    print_import(out, header, fwd, mod, node, force);
    break;
  case WITHIN:
  case DEFINCOMPLETE:
    break;
  default:
    fprintf(g_env.stderr, "Unsupported node: %d\n", node->which);
    assert(false);
  }

  out->node = NULL;
  --prevent_infinite;
}

static void print_mod_includes(struct out *out, bool header, const struct module *mod) {
  if (header) {
    if (file_exists(mod->filename, ".h")) {
      print_include(out, mod->filename, ".h");
    }
  } else {
    print_include(out, mod->filename, ".o.h");
    if (file_exists(mod->filename, ".c")) {
      print_include(out, mod->filename, ".c");
    }
  }
}

static void print_module(struct out *out, bool header, const struct module *mod) {
  out->mod = mod;
  mod->stage->printing_mod = mod;

  pf(out, "#include <lib/n/runtime.h>\n");

  struct fintypset printed;
  fintypset_fullinit(&printed);

  const enum forward fwd_passes[] = {
    FWD_DECLARE_TYPES, FWD_DEFINE_DYNS, FWD_DEFINE_TYPES,
    FWD_DECLARE_FUNCTIONS, FWD_DEFINE_FUNCTIONS };
  for (int i = 0; i < ARRAY_SIZE(fwd_passes); ++i) {
    enum forward fwd = fwd_passes[i];

    if (header) {
      pf(out, "#ifdef %s\n", forward_guards[fwd]);
      pf(out, "#ifndef %s__", forward_guards[fwd]);
      print_scope_name(out, mod, mod->root);
      pf(out, "\n#define %s__", forward_guards[fwd]);
      print_scope_name(out, mod, mod->root);
      pf(out, "\n\n");
    } else {
      pf(out, "#define %s\n", forward_guards[fwd]);
    }

    FOREACH_SUB_CONST(node, mod->body) {
      if (node->which != IMPORT) {
        break;
      }
      print_import(out, header, fwd, mod, node, false);
    }

    print_mod_includes(out, header, mod);

    if (!header) {
      struct useorder uorder = { 0 };
      useorder_build(&uorder, mod, header, fwd);
#if 0
      fprintf(stderr, "%d %s\n", header, mod->filename);
      if (!header && strcmp(mod->filename, "lib/n/env/env.n")==0) {
        debug_useorder_print(&uorder);
      }
#endif

      for (size_t n = 0, count = vecnode_count(&uorder.dependencies); n < count; ++n) {
        const struct node *node = *vecnode_get(&uorder.dependencies, n);
        const struct module *dmod = node_module_owner_const(node);

        const struct module *old_mod = out->mod;
        out->mod = dmod;
        print_top(out, header, fwd, dmod, node, &printed, false);
        out->mod = old_mod;

        pf(out, "\n");
      }

      useorder_destroy(&uorder);
    }

    if (header) {
      pf(out, "\n#endif\n");
      pf(out, "#endif // %s\n", forward_guards[fwd]);
    } else {
      pf(out, "#undef %s\n", forward_guards[fwd]);
    }
  }

  fintypset_destroy(&printed);
}

static void print_runexamples(struct out *out, const struct module *mod) {
  pf(out, "void ");
  print_c_runexamples_name(out->c, mod);
  pf(out, "(void) " SECTION_EXAMPLES ";\n");

  pf(out, "void ");
  print_c_runexamples_name(out->c, mod);
  pf(out, "(void) {\n");

  FOREACH_SUB_CONST(ex, mod->body) {
    if (ex->which != DEFFUN
        || ex->as.DEFFUN.example == 0) {
      continue;
    }

    const size_t arity = typ_function_arity(ex->typ);
    if (arity > 1) {
      assert(false);
    }

    pf(out, "{\n");
    if (arity == 1) {
      pf(out, "NLANG_EXAMPLE_PRE;\n");
    }
    pf(out, "struct n$builtins$Error err = ");
    print_scope_name(out, mod, ex);
    pf(out, "(%s);\n", arity == 1 ? "&ex" : "");
    pf(out, "NLANG_EXAMPLE_EXCEPT(\"");
    print_scope_name(out, mod, ex);
    pf(out, "\", err);\n");
    if (arity == 1) {
      pf(out, "NLANG_EXAMPLE_POST;\n");
    }
    pf(out, "}\n");
  }
  pf(out, "}\n");
}

error printer_c(int fd, int linemap_fd, const struct module *mod) {
  struct out out = { 0 };

  out.c = fdopen(fd, "w");
  if (out.c == NULL) {
    THROWF(errno, "Invalid C output file descriptor '%d'", fd);
  }

  out.linemap = fdopen(linemap_fd, "w");
  if (out.linemap == NULL) {
    THROWF(errno, "Invalid linemap output file descriptor '%d'", linemap_fd);
  }
  // Make sure the first file listed is the primary.
  fprintf(out.linemap, "1 1 %s\n", mod->filename);

  print_module(&out, false, mod);
  print_runexamples(&out, mod);
  fflush(out.c);
  fflush(out.linemap);

  return 0;
}

error printer_h(int fd, const struct module *mod) {
  struct out out = { 0 };

  out.c = fdopen(fd, "w");
  if (out.c == NULL) {
    THROWF(errno, "Invalid output file descriptor '%d'", fd);
  }

  print_module(&out, true, mod);
  fflush(out.c);
  fflush(out.linemap);

  return 0;
}

void print_c_runexamples_name(FILE *outc, const struct module *mod) {
  struct out out = { .c = outc };
  print_scope_name(&out, mod, mod->root);
  pf(&out, "_$Nrunexamples");
}
