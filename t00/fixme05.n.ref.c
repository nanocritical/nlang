#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/fixme05.n.o.h"
#undef NLANG_DECLARE_TYPES
struct t00_fixme05_nocopy;
typedef struct t00_fixme05_nocopy t00_fixme05_nocopy;
#ifndef HAS0__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
#define HAS0__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
struct t00_fixme05_nocopy;
typedef struct t00_fixme05_nocopy* _Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_;
#endif // HAS0__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
;



#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/fixme05.n.o.h"
#undef NLANG_DEFINE_TYPES
struct t00_fixme05_nocopy {
nlang_builtins_i32 x;
}
;



#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/fixme05.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#ifndef HAS2__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
#define HAS2__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
struct t00_fixme05_nocopy;
typedef struct t00_fixme05_nocopy* _Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_;
#endif // HAS2__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
nlang_builtins_void t00_fixme05_nocopy_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_ self) __attribute__((__nonnull__(1)));
;
void t00_fixme05_nocopy_mk(t00_fixme05_nocopy *_nrtr_r);
;
_Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_ t00_fixme05_nocopy_new(void);
;

static void t00_fixme05_bar(t00_fixme05_nocopy *_nrtr_r);

static void t00_fixme05_foo(t00_fixme05_nocopy *_nrtr__nretval);

#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/fixme05.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
#ifndef HAS3__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
#define HAS3__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
struct t00_fixme05_nocopy;
typedef struct t00_fixme05_nocopy* _Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_;
#endif // HAS3__Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_
nlang_builtins_void t00_fixme05_nocopy_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_ self) {
#define THIS(x) t00_fixme05_nocopy##x
#undef THIS
}
;
void t00_fixme05_nocopy_mk(t00_fixme05_nocopy *_nrtr_r) {
#define THIS(x) t00_fixme05_nocopy##x
#define r (*_nrtr_r)
#undef r
#undef THIS
}
;
_Ngen_nlang_builtins_mercurial_ref__t00_fixme05_nocopy_genN_ t00_fixme05_nocopy_new(void) {
#define THIS(x) t00_fixme05_nocopy##x
return calloc(1, sizeof(THIS()));
#undef THIS
}
;

static void t00_fixme05_bar(t00_fixme05_nocopy *_nrtr_r) {
#define r (*_nrtr_r)
 {
(r.x) = (nlang_builtins_i32)0;
;
return;
}
#undef r
}

static void t00_fixme05_foo(t00_fixme05_nocopy *_nrtr__nretval) {
#define _nretval (*_nrtr__nretval)
 {
t00_fixme05_nocopy r = { 0 };
(r.x) = (nlang_builtins_i32)0;
;
;
return;
}
#undef _nretval
}

nlang_builtins_i32 _Nmain(void) {
 {
t00_fixme05_nocopy _Ngensym0 = { 0 };
return (({ t00_fixme05_foo(&(_Ngensym0));
_Ngensym0;
; }).x);
;
}
}
void t00_fixme05_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_fixme05_Nrunexamples(void) {
}