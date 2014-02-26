#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/dynintf.n.o.h"
#undef NLANG_DECLARE_TYPES
struct _Ndyn_t00_dynintf__Ni_d;
struct t00_dynintf__Ni_d;
typedef struct t00_dynintf__Ni_d t00_dynintf__Ni_d;

struct t00_dynintf_d;
typedef struct t00_dynintf_d t00_dynintf_d;
#ifndef HAS0__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
#define HAS0__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef const struct t00_dynintf_d* _Ngen_nlang_builtins_ref__t00_dynintf_d_genN_;
#endif // HAS0__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
;
#ifndef HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
#define HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef struct t00_dynintf_d* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_;
#endif // HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
;

struct t00_dynintf_dc;
typedef struct t00_dynintf_dc t00_dynintf_dc;
#ifndef HAS0__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
#define HAS0__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef const struct t00_dynintf_dc* _Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_;
#endif // HAS0__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
;
#ifndef HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
#define HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef struct t00_dynintf_dc* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_;
#endif // HAS0__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
;

#ifndef HAS0_t00_dynintf__Ni_d
#define HAS0_t00_dynintf__Ni_d
#endif // HAS0_t00_dynintf__Ni_d




#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/dynintf.n.o.h"
#undef NLANG_DEFINE_TYPES
struct _Ndyn_t00_dynintf__Ni_d {
nlang_builtins_i32 (*answer)(void);
nlang_builtins_i32 (*get)(void *self);
};
struct t00_dynintf__Ni_d {
const struct _Ndyn_t00_dynintf__Ni_d *vptr;
void *obj;
};

struct t00_dynintf_d {
nlang_builtins_i32 x;
}
;

struct t00_dynintf_dc {
nlang_builtins_i32 dummy;
}
;





#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/dynintf.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
nlang_builtins_i32 t00_dynintf__Ni_d_answer(t00_dynintf__Ni_d self);
nlang_builtins_i32 t00_dynintf__Ni_d_get(t00_dynintf__Ni_d self);

nlang_builtins_i32 t00_dynintf_d_answer(void);
;
#ifndef HAS2__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
#define HAS2__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef const struct t00_dynintf_d* _Ngen_nlang_builtins_ref__t00_dynintf_d_genN_;
#endif // HAS2__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
nlang_builtins_i32 t00_dynintf_d_get(_Ngen_nlang_builtins_ref__t00_dynintf_d_genN_ self) __attribute__((__nonnull__(1)));
;
#ifndef HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
#define HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef struct t00_dynintf_d* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_;
#endif // HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
nlang_builtins_void t00_dynintf_d_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_ self) __attribute__((__nonnull__(1)));
;
void t00_dynintf_d_mk(t00_dynintf_d *_nrtr_r);
;
_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_ t00_dynintf_d_new(void);
;
static inline t00_dynintf__Ni_d t00_dynintf_d_mkdyn__t00_dynintf__Ni_d(t00_dynintf_d *obj);

nlang_builtins_i32 t00_dynintf_dc_answer(void);
;
#ifndef HAS2__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
#define HAS2__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef const struct t00_dynintf_dc* _Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_;
#endif // HAS2__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
nlang_builtins_i32 t00_dynintf_dc_get(_Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_ self) __attribute__((__nonnull__(1)));
;
#ifndef HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
#define HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef struct t00_dynintf_dc* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_;
#endif // HAS2__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
nlang_builtins_void t00_dynintf_dc_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_ self) __attribute__((__nonnull__(1)));
;
void t00_dynintf_dc_mk(t00_dynintf_dc *_nrtr_r);
;
_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_ t00_dynintf_dc_new(void);
;
static inline t00_dynintf__Ni_d t00_dynintf_dc_mkdyn__t00_dynintf__Ni_d(t00_dynintf_dc *obj);

#ifndef HAS2_t00_dynintf__Ni_d
#define HAS2_t00_dynintf__Ni_d
#endif // HAS2_t00_dynintf__Ni_d
static nlang_builtins_i32 t00_dynintf_foo(t00_dynintf__Ni_d v);

void t00_dynintf__Nexample0(void) __attribute__((section(".text.nlang.examples")));
static nlang_builtins_i32 t00_dynintf_rfoo(t00_dynintf__Ni_d pv);

static nlang_builtins_i32 t00_dynintf_bar(_Ngen_nlang_builtins_ref__t00_dynintf_d_genN_ pv) __attribute__((__nonnull__(1)));

#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/dynintf.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
nlang_builtins_i32 t00_dynintf__Ni_d_answer(t00_dynintf__Ni_d self) {
return self.vptr->answer();
}
nlang_builtins_i32 t00_dynintf__Ni_d_get(t00_dynintf__Ni_d self) {
return self.vptr->get(self.obj);
}

nlang_builtins_i32 t00_dynintf_d_answer(void) {
#define THIS(x) t00_dynintf_d##x
 {
return (nlang_builtins_i32)42;
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
#define HAS3__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef const struct t00_dynintf_d* _Ngen_nlang_builtins_ref__t00_dynintf_d_genN_;
#endif // HAS3__Ngen_nlang_builtins_ref__t00_dynintf_d_genN_
nlang_builtins_i32 t00_dynintf_d_get(_Ngen_nlang_builtins_ref__t00_dynintf_d_genN_ self) {
#define THIS(x) t00_dynintf_d##x
 {
return (self->x);
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
#define HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
struct t00_dynintf_d;
typedef struct t00_dynintf_d* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_;
#endif // HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_
nlang_builtins_void t00_dynintf_d_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_ self) {
#define THIS(x) t00_dynintf_d##x
#undef THIS
}
;
void t00_dynintf_d_mk(t00_dynintf_d *_nrtr_r) {
#define THIS(x) t00_dynintf_d##x
#define r (*_nrtr_r)
#undef r
#undef THIS
}
;
_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_d_genN_ t00_dynintf_d_new(void) {
#define THIS(x) t00_dynintf_d##x
return calloc(1, sizeof(THIS()));
#undef THIS
}
;
static inline t00_dynintf__Ni_d t00_dynintf_d_mkdyn__t00_dynintf__Ni_d(t00_dynintf_d *obj) {
static const struct _Ndyn_t00_dynintf__Ni_d vtable = {
.answer = (nlang_builtins_i32 (*)(void))t00_dynintf_d_answer,
.get = (nlang_builtins_i32 (*)(void *self))t00_dynintf_d_get,
};
return (t00_dynintf__Ni_d){ .vptr = &vtable, .obj = obj };
}

nlang_builtins_i32 t00_dynintf_dc_answer(void) {
#define THIS(x) t00_dynintf_dc##x
 {
return (nlang_builtins_i32)42;
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
#define HAS3__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef const struct t00_dynintf_dc* _Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_;
#endif // HAS3__Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_
nlang_builtins_i32 t00_dynintf_dc_get(_Ngen_nlang_builtins_ref__t00_dynintf_dc_genN_ self) {
#define THIS(x) t00_dynintf_dc##x
 {
return (nlang_builtins_i32)42;
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
#define HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
struct t00_dynintf_dc;
typedef struct t00_dynintf_dc* _Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_;
#endif // HAS3__Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_
nlang_builtins_void t00_dynintf_dc_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_ self) {
#define THIS(x) t00_dynintf_dc##x
#undef THIS
}
;
void t00_dynintf_dc_mk(t00_dynintf_dc *_nrtr_r) {
#define THIS(x) t00_dynintf_dc##x
#define r (*_nrtr_r)
#undef r
#undef THIS
}
;
_Ngen_nlang_builtins_mercurial_ref__t00_dynintf_dc_genN_ t00_dynintf_dc_new(void) {
#define THIS(x) t00_dynintf_dc##x
return calloc(1, sizeof(THIS()));
#undef THIS
}
;
static inline t00_dynintf__Ni_d t00_dynintf_dc_mkdyn__t00_dynintf__Ni_d(t00_dynintf_dc *obj) {
static const struct _Ndyn_t00_dynintf__Ni_d vtable = {
.answer = (nlang_builtins_i32 (*)(void))t00_dynintf_dc_answer,
.get = (nlang_builtins_i32 (*)(void *self))t00_dynintf_dc_get,
};
return (t00_dynintf__Ni_d){ .vptr = &vtable, .obj = obj };
}

#ifndef HAS3_t00_dynintf__Ni_d
#define HAS3_t00_dynintf__Ni_d
#endif // HAS3_t00_dynintf__Ni_d
static nlang_builtins_i32 t00_dynintf_foo(t00_dynintf__Ni_d v) {
 {
return t00_dynintf__Ni_d_get(v);
}
}

void t00_dynintf__Nexample0(void) {
({ nlang_builtins___example__(({ t00_dynintf_d x= { 0 };
x.x = (nlang_builtins_i32)42;
;
;
((nlang_builtins_i32)42 == t00_dynintf_foo(t00_dynintf_d_mkdyn__t00_dynintf__Ni_d((void *)((&x)))));
; }));
; });
}
static nlang_builtins_i32 t00_dynintf_rfoo(t00_dynintf__Ni_d pv) {
 {
return t00_dynintf__Ni_d_answer(*(t00_dynintf__Ni_d *)&pv);
}
}

static nlang_builtins_i32 t00_dynintf_bar(_Ngen_nlang_builtins_ref__t00_dynintf_d_genN_ pv) {
 {
return t00_dynintf_foo(t00_dynintf_d_mkdyn__t00_dynintf__Ni_d((void *)pv));
}
}

nlang_builtins_i32 _Nmain(void) {
 {
t00_dynintf_d xd= { 0 };
xd.x = (nlang_builtins_i32)42;
;
;
t00_dynintf_dc xdc= { 0 };
;
;
( ((t00_dynintf_foo(t00_dynintf_d_mkdyn__t00_dynintf__Ni_d((void *)((&xd)))) != t00_dynintf_foo(t00_dynintf_dc_mkdyn__t00_dynintf__Ni_d((void *)((&xdc)))))) ? ( {
return (nlang_builtins_i32)1;
}) : ({;}) );
_Ngen_nlang_builtins_ref__t00_dynintf_d_genN_ pxd = ((&xd));
;
( ((t00_dynintf_bar(pxd) != t00_dynintf_rfoo(t00_dynintf_d_mkdyn__t00_dynintf__Ni_d((void *)pxd)))) ? ( {
return (nlang_builtins_i32)1;
}) : ({;}) );
return (nlang_builtins_i32)0;
}
}
void t00_dynintf_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_dynintf_Nrunexamples(void) {
t00_dynintf__Nexample0();
}