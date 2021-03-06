#ifndef LEXER_H__
#define LEXER_H__

#include <stdint.h>
#include <stdlib.h>

#include "common.h"

enum token_type {
  Timport = 1,
  Texport,
  Tfrom,
  Tbuild,
  Tstruct,
  Tenum,
  Tunion,
  Textern,
  Topaque,
  Tfun,
  Tmethod,
  Tshallow,
  Tintf,
  Tnewtype,
  Tinline,
  Tlet,
  Tsuch,
  Talias,
  Tif,
  Telif,
  Telse,
  Tforeach,
  Tfor,
  Tover,
  Twhile,
  Tcontinue,
  Tbreak,
  Tmatch,
  Treturn,
  Ttry,
  Tcatch,
  Texcept,
  Tdrop,
  Tfatal,
  Tnever,
  Tthrow,
  Tblock,
  Tdeclare,
  Tand,
  Tor,
  Tnot,
  Tfalse,
  Ttrue,
  Tin,
  Tisa,
  Tnil,
  Tnoop,
  Tassert,
  Tpre,
  Tpost,
  Tinvariant,
  Texample,
  Tsizeof,
  Talignof,
  Twithin,
  Tglobalenv,

  TNUMBER,
  TSTRING,
  TIDENT,
  TASSIGN,
  TEQ,
  TNE,
  TEQPTR,
  TNEPTR,
  TEQMATCH,
  TNEMATCH,
  TLE,
  TLT,
  TGT,
  TGE,
  TPLUS,
  TMINUS,
  TUPLUS,
  TUMINUS,
  TTIMES,
  TDIVIDE,
  TMODULO,
  TOVPLUS,
  TOVMINUS,
  TOVUPLUS,
  TOVUMINUS,
  TOVTIMES,
  TOVDIVIDE,
  TOVMODULO,
  TBWAND,
  TBWOR,
  TBWXOR,
  TRSHIFT,
  TOVLSHIFT,
  TPLUS_ASSIGN,
  TMINUS_ASSIGN,
  TTIMES_ASSIGN,
  TDIVIDE_ASSIGN,
  TMODULO_ASSIGN,
  TOVPLUS_ASSIGN,
  TOVMINUS_ASSIGN,
  TOVTIMES_ASSIGN,
  TOVDIVIDE_ASSIGN,
  TOVMODULO_ASSIGN,
  TBWAND_ASSIGN,
  TBWOR_ASSIGN,
  TBWXOR_ASSIGN,
  TRSHIFT_ASSIGN,
  TOVLSHIFT_ASSIGN,
  TBWNOT,
  TARROW,
  TBARROW,
  TEOL,
  TSOB,
  TEOB,
  TCOLON,
  TDCOLON,
  TCOMMA,
  TSEMICOLON,
  TPREDOT,
  TDOT,
  TBANG,
  TSHARP,
  TWILDCARD,
  TATDOT,
  TATBANG,
  TATWILDCARD,
  TDEREFDOT,
  TDEREFBANG,
  TDEREFSHARP,
  TDEREFWILDCARD,
  TOPTDOT,
  TOPTBANG,
  TOPTSHARP,
  TOPTWILDCARD,
  TOPTATDOT,
  TOPTATBANG,
  TOPTATWILDCARD,
  TREFDOT,
  TREFBANG,
  TREFSHARP,
  TREFWILDCARD,
  TNULREFDOT,
  TNULREFBANG,
  TNULREFSHARP,
  TNULREFWILDCARD,
  TCREFDOT,
  TCREFBANG,
  TCREFSHARP,
  TCREFWILDCARD,
  TCREFDOT_POST,
  TCREFBANG_POST,
  TCREFSHARP_POST,
  TCREFWILDCARD_POST,
  TREFCREFWILDCARD, // fun/method access only
  TNULCREFDOT,
  TNULCREFBANG,
  TNULCREFSHARP,
  TNULCREFWILDCARD,
  TPREQMARK,
  TPOSTQMARK,
  TDOTDOT,
  TBEGDOTDOT,
  TENDDOTDOT,
  TDOTDOTDOT,
  TSLICEBRAKETS,
  TMSLICEBRAKETS,
  TWSLICEBRAKETS,
  TLCBRA,
  TRCBRA,
  TRSBRA,
  TLPAR,
  TRPAR,

  // We rewrite (Nullable x) as a unary operator in LIR, even though
  // syntactically it's a function call, because we don't want the
  // complexity of typing it as a generic function, and we don't want to use
  // up a precious enum node_which entry. This is purely internal and could
  // change.
  T__NULLABLE,
  T__NONNULLABLE,
  T__DEOPT,
  T__DEOPT_DEREFDOT,

  T__CALL,
  T__NOT_STATEMENT,
  T__STATEMENT,
  T__NOT_COLON,
  T__NOT_COMMA,
  T__NOT_OVER,

  TOKEN__NUM,
};

const char *token_strings[TOKEN__NUM];

enum operator_associativity {
  ASSOC_NON = 0x1,
  ASSOC_LEFT = 0x2,
  ASSOC_LEFT_SAME = 0x3,
  ASSOC_RIGHT = 0x4,
};

enum operator_kind {
  OP_UN_REFOF = 0x1,
  OP_UN_DEREF = 0x2,
  OP_UN_BOOL = 0x3,
  OP_UN_ADDARITH = 0x4,
  OP_UN_OVARITH = 0x5,
  OP_UN_BW = 0x6,
  OP_UN_SLICE = 0x7,
  OP_UN_RANGE = 0x8,
  OP_UN_PRIMITIVES = 0x9,
  OP_UN_OPT = 0xa,
  OP_BIN = 0x10,
  OP_BIN_SYM = 0x20,
  OP_BIN_SYM_BOOL = 0x30,
  OP_BIN_SYM_ADDARITH = 0x40,
  OP_BIN_SYM_ARITH = 0x50,
  OP_BIN_SYM_INTARITH = 0x60,
  OP_BIN_SYM_OVARITH = 0x70,
  OP_BIN_SYM_BW = 0x80,
  OP_BIN_SYM_PTR = 0x90,
  OP_BIN_ACC = 0xa0,
  OP_BIN_OPT_ACC = 0xb0,
  OP_BIN_BW_RHS_UNSIGNED = 0xc0,
  OP_BIN_OVBW_RHS_UNSIGNED = 0xd0,
  OP_BIN_RHS_TYPE = 0xe0,
  OP_UN__MASK = 0x0f,
  OP_BIN__MASK = 0xf0,
  OP_KIND__MASK = 0xff,
};

struct operator {
  unsigned prec :16;
  unsigned kind :8;
  unsigned is_assign :1;
  unsigned assoc :3;
};

#define OP_ASSIGN 0x1

#define OP(assgn, knd, assc, precedence) \
{ \
  .prec = (precedence), \
  .kind = (knd), \
  .assoc = (assc), \
  .is_assign = (assgn), \
}

#define IS_OP(t) (operators[t].prec != 0)
#define OP_IS_UNARY(t) (!!(operators[t].kind & OP_UN__MASK))
#define OP_IS_BINARY(t) (!!(operators[t].kind & OP_BIN__MASK))
#define OP_ASSOC(t) (operators[t].assoc)
#define OP_PREC(t) (operators[t].prec)
#define OP_KIND(t) (operators[t].kind & OP_KIND__MASK)
#define OP_IS_ASSIGN(t) (operators[t].is_assign)

static const struct operator operators[TOKEN__NUM] = {
  [T__STATEMENT] = OP(0, 0, 0, 0xffff),
  [T__NULLABLE] = OP(0, OP_UN_PRIMITIVES, 0, 0xf000),
  [T__NONNULLABLE] = OP(0, OP_UN_PRIMITIVES, 0, 0xf000),
  [T__DEOPT] = OP(0, OP_UN_OPT, 0, 0xf000),
  [T__DEOPT_DEREFDOT] = OP(0, OP_UN_OPT, 0, 0xf000),
  [TASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM, ASSOC_NON, 0x140),
  [TPLUS_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_ADDARITH, ASSOC_NON, 0x140),
  [TMINUS_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_ADDARITH, ASSOC_NON, 0x140),
  [TTIMES_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_ARITH, ASSOC_NON, 0x140),
  [TDIVIDE_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_ARITH, ASSOC_NON, 0x140),
  [TMODULO_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_INTARITH, ASSOC_NON, 0x140),
  [TOVPLUS_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_OVARITH, ASSOC_NON, 0x140),
  [TOVMINUS_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_OVARITH, ASSOC_NON, 0x140),
  [TOVTIMES_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_OVARITH, ASSOC_NON, 0x140),
  [TOVDIVIDE_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_OVARITH, ASSOC_NON, 0x140),
  [TOVMODULO_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_OVARITH, ASSOC_NON, 0x140),
  [TBWAND_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_BW, ASSOC_NON, 0x140),
  [TBWOR_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_BW, ASSOC_NON, 0x140),
  [TBWXOR_ASSIGN] = OP(OP_ASSIGN, OP_BIN_SYM_BW, ASSOC_NON, 0x140),
  [TRSHIFT_ASSIGN] = OP(OP_ASSIGN, OP_BIN_BW_RHS_UNSIGNED, ASSOC_NON, 0x140),
  [TOVLSHIFT_ASSIGN] = OP(OP_ASSIGN, OP_BIN_OVBW_RHS_UNSIGNED, ASSOC_NON, 0x140),
  [Tinvariant] = OP(0, OP_UN_PRIMITIVES, ASSOC_NON, 0x137),
  [Tpost] = OP(0, OP_UN_PRIMITIVES, ASSOC_NON, 0x137),
  [Tpre] = OP(0, OP_UN_PRIMITIVES, ASSOC_NON, 0x137),
  [Tassert] = OP(0, OP_UN_PRIMITIVES, ASSOC_NON, 0x137),
  [T__NOT_STATEMENT] = OP(0, OP_BIN, ASSOC_NON, 0x135),
  [Tover] = OP(0, OP_BIN, ASSOC_NON, 0x133),
  [T__NOT_OVER] = OP(0, OP_BIN, ASSOC_NON, 0x132),
  [TCOMMA] = OP(0, OP_BIN, ASSOC_LEFT, 0x131),
  [T__NOT_COMMA] = OP(0, OP_BIN, ASSOC_NON, 0x130),
  [Telse] = OP(0, OP_BIN_SYM_PTR, ASSOC_LEFT, 0x123),
  [Tor] = OP(0, OP_BIN_SYM_BOOL, ASSOC_LEFT, 0x122),
  [Tand] = OP(0, OP_BIN_SYM_BOOL, ASSOC_LEFT, 0x121),
  [Tnot] = OP(0, OP_UN_BOOL, ASSOC_RIGHT, 0x120),
  [Tin] = OP(0, OP_BIN, ASSOC_NON, 0x110),
  [TLE] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TLT] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TGT] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TGE] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TEQ] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TNE] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TEQPTR] = OP(0, OP_BIN_SYM_PTR, ASSOC_NON, 0xe0),
  [TNEPTR] = OP(0, OP_BIN_SYM_PTR, ASSOC_NON, 0xe0),
  [TEQMATCH] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [TNEMATCH] = OP(0, OP_BIN_SYM, ASSOC_NON, 0xe0),
  [Tisa] = OP(0, OP_BIN_RHS_TYPE, ASSOC_NON, 0xd7),
  [T__CALL] = OP(0, OP_BIN, ASSOC_NON, 0xd0),
  [TBARROW] = OP(0, OP_BIN, ASSOC_RIGHT, 0xcf),
  [TDOTDOT] = OP(0, OP_BIN, ASSOC_NON, 0x96),
  [TBEGDOTDOT] = OP(0, OP_UN_RANGE, ASSOC_NON, 0x95),
  [TENDDOTDOT] = OP(0, OP_UN_RANGE, ASSOC_NON, 0x95),
  [TBWOR] = OP(0, OP_BIN_SYM_BW, ASSOC_LEFT_SAME, 0x90),
  [TBWXOR] = OP(0, OP_BIN_SYM_BW, ASSOC_LEFT_SAME, 0x90),
  [TBWAND] = OP(0, OP_BIN_SYM_BW, ASSOC_LEFT_SAME, 0x90),
  [TOVLSHIFT] = OP(0, OP_BIN_OVBW_RHS_UNSIGNED, ASSOC_NON, 0x90),
  [TRSHIFT] = OP(0, OP_BIN_BW_RHS_UNSIGNED, ASSOC_NON, 0x90),
  [TPLUS] = OP(0, OP_BIN_SYM_ADDARITH, ASSOC_LEFT, 0x80),
  [TOVPLUS] = OP(0, OP_BIN_SYM_OVARITH, ASSOC_LEFT, 0x80),
  [TMINUS] = OP(0, OP_BIN_SYM_ADDARITH, ASSOC_LEFT, 0x80),
  [TOVMINUS] = OP(0, OP_BIN_SYM_OVARITH, ASSOC_LEFT, 0x80),
  [TDIVIDE] = OP(0, OP_BIN_SYM_ARITH, ASSOC_LEFT, 0x60),
  [TOVDIVIDE] = OP(0, OP_BIN_SYM_OVARITH, ASSOC_LEFT, 0x60),
  [TMODULO] = OP(0, OP_BIN_SYM_INTARITH, ASSOC_LEFT, 0x60),
  [TOVMODULO] = OP(0, OP_BIN_SYM_OVARITH, ASSOC_LEFT, 0x60),
  [TTIMES] = OP(0, OP_BIN_SYM_ARITH, ASSOC_LEFT, 0x60),
  [TOVTIMES] = OP(0, OP_BIN_SYM_OVARITH, ASSOC_LEFT, 0x60),
  [TUPLUS] = OP(0, OP_UN_ADDARITH, ASSOC_NON, 0x50),
  [TOVUPLUS] = OP(0, OP_UN_OVARITH, ASSOC_NON, 0x50),
  [TUMINUS] = OP(0, OP_UN_ADDARITH, ASSOC_NON, 0x50),
  [TOVUMINUS] = OP(0, OP_UN_OVARITH, ASSOC_NON, 0x50),
  [TBWNOT] = OP(0, OP_UN_BW, ASSOC_NON, 0x50),
  [TPREQMARK] = OP(0, OP_UN_OPT, ASSOC_RIGHT, 0x40),
  [TREFDOT] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TREFBANG] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TREFSHARP] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TREFWILDCARD] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULREFDOT] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULREFBANG] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULREFSHARP] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULREFWILDCARD] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFDOT] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFBANG] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFSHARP] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFWILDCARD] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFDOT_POST] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFBANG_POST] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFSHARP_POST] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TCREFWILDCARD_POST] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TREFCREFWILDCARD] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULCREFDOT] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULCREFBANG] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULCREFSHARP] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TNULCREFWILDCARD] = OP(0, OP_UN_REFOF, ASSOC_RIGHT, 0x40),
  [TSLICEBRAKETS] = OP(0, OP_UN_SLICE, ASSOC_RIGHT, 0x40),
  [TMSLICEBRAKETS] = OP(0, OP_UN_SLICE, ASSOC_RIGHT, 0x40),
  [TWSLICEBRAKETS] = OP(0, OP_UN_SLICE, ASSOC_RIGHT, 0x40),
  [TDCOLON] = OP(0, OP_BIN_RHS_TYPE, ASSOC_NON, 0x31),
  [TCOLON] = OP(0, OP_BIN_RHS_TYPE, ASSOC_NON, 0x30),
  [T__NOT_COLON] = OP(0, OP_BIN, ASSOC_NON, 0x29),
  [TPOSTQMARK] = OP(0, OP_UN_OPT, ASSOC_NON, 0x21),
  [TDEREFDOT] = OP(0, OP_UN_DEREF, ASSOC_LEFT, 0x20),
  [TDEREFBANG] = OP(0, OP_UN_DEREF, ASSOC_LEFT, 0x20),
  [TDEREFSHARP] = OP(0, OP_UN_DEREF, ASSOC_LEFT, 0x20),
  [TDEREFWILDCARD] = OP(0, OP_UN_DEREF, ASSOC_LEFT, 0x20),
  [TOPTATDOT] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TATDOT] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TOPTATBANG] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TATBANG] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TOPTATWILDCARD] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TATWILDCARD] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TOPTDOT] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TDOT] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TOPTBANG] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TBANG] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TOPTSHARP] = OP(0, OP_BIN_OPT_ACC, ASSOC_LEFT, 0x10),
  [TSHARP] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TWILDCARD] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
  [TWILDCARD] = OP(0, OP_BIN_ACC, ASSOC_LEFT, 0x10),
};

static const bool expr_terminators[TOKEN__NUM] = {
  [TEOL] = true,
  [TSOB] = true,
  [TEOB] = true,
  [TRPAR] = true,
  [TRCBRA] = true,
  [TRSBRA] = true,
  [Twithin] = true,
  [Tsuch] = true,
};

struct codeloc {
  size_t pos;
  int line;
  int column;
};

enum block_style {
  BLOCK_MULTI = 0,
  BLOCK_SINGLE,
};

struct parser {
  const char *data;
  size_t len;
  struct codeloc codeloc;

  size_t indent;
  size_t block_depth;
  enum block_style block_style[128];

  bool tok_was_injected;
  bool inject_eol_after_eob;

  size_t next_component_first_pos;
  size_t current_component;

  char error_message[1024];
};

struct token {
  enum token_type t;
  const char *value;
  size_t len;
};

ERROR lexer_scan(struct token *tok, struct parser *parser);
void lexer_back(struct parser *parser, const struct token *tok);

ERROR lexer_open_implicit_single_block(struct parser *parser);

#endif
