#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/returnthroughref.n.o.h"
#undef NLANG_DECLARE_TYPES
struct t00_returnthroughref_t;
typedef struct t00_returnthroughref_t t00_returnthroughref_t;
#ifndef HAS0__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
#define HAS0__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
struct t00_returnthroughref_t;
typedef struct t00_returnthroughref_t* _Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_;
#endif // HAS0__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
;




#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/returnthroughref.n.o.h"
#undef NLANG_DEFINE_TYPES
struct t00_returnthroughref_t {
nlang_builtins_i32 x;
nlang_builtins_i32 xx;
}
;




#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/returnthroughref.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#ifndef HAS2__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
#define HAS2__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
struct t00_returnthroughref_t;
typedef struct t00_returnthroughref_t* _Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_;
#endif // HAS2__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
nlang_builtins_void t00_returnthroughref_t_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_ self) __attribute__((__nonnull__(1)));
;
void t00_returnthroughref_t_mk(t00_returnthroughref_t *_nrtr_r);
;
_Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_ t00_returnthroughref_t_new(void);
;

static void t00_returnthroughref_foo(t00_returnthroughref_t *_nrtr__nretval);

static void t00_returnthroughref_bar(t00_returnthroughref_t *_nrtr__nretval);

static void t00_returnthroughref_bar2(t00_returnthroughref_t *_nrtr_r);

#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/returnthroughref.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
#ifndef HAS3__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
#define HAS3__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
struct t00_returnthroughref_t;
typedef struct t00_returnthroughref_t* _Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_;
#endif // HAS3__Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_
nlang_builtins_void t00_returnthroughref_t_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_ self) {
#define THIS(x) t00_returnthroughref_t##x
#undef THIS
}
;
void t00_returnthroughref_t_mk(t00_returnthroughref_t *_nrtr_r) {
#define THIS(x) t00_returnthroughref_t##x
#define r (*_nrtr_r)
#undef r
#undef THIS
}
;
_Ngen_nlang_builtins_mercurial_ref__t00_returnthroughref_t_genN_ t00_returnthroughref_t_new(void) {
#define THIS(x) t00_returnthroughref_t##x
return calloc(1, sizeof(THIS()));
#undef THIS
}
;

static void t00_returnthroughref_foo(t00_returnthroughref_t *_nrtr__nretval) {
#define _nretval (*_nrtr__nretval)
 {
_nretval.x = (nlang_builtins_i32)3;
;
return;
}
#undef _nretval
}

static void t00_returnthroughref_bar(t00_returnthroughref_t *_nrtr__nretval) {
#define _nretval (*_nrtr__nretval)
 {
t00_returnthroughref_foo(&(_nretval));
return;
}
#undef _nretval
}

static void t00_returnthroughref_bar2(t00_returnthroughref_t *_nrtr_r) {
#define r (*_nrtr_r)
 {
t00_returnthroughref_bar(&(r));
;
return;
}
#undef r
}

nlang_builtins_i32 _Nmain(void) {
 {
t00_returnthroughref_t y = { 0 };
t00_returnthroughref_bar(&(y));
;
t00_returnthroughref_t _Ngensym1 = { 0 };
( (((y.x) != (({ t00_returnthroughref_bar2(&(_Ngensym1));
_Ngensym1;
; }).x))) ? ( {
return (nlang_builtins_i32)1;
}) : ({;}) );
;
return (y.xx);
}
}
void t00_returnthroughref_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_returnthroughref_Nrunexamples(void) {
}