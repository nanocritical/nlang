#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/method.n.o.h"
#undef NLANG_DECLARE_TYPES
struct t00_method_t;
typedef struct t00_method_t t00_method_t;
#ifndef HAS0__Ngen_nlang_builtins_ref__t00_method_t_genN_
#define HAS0__Ngen_nlang_builtins_ref__t00_method_t_genN_
struct t00_method_t;
typedef const struct t00_method_t* _Ngen_nlang_builtins_ref__t00_method_t_genN_;
#endif // HAS0__Ngen_nlang_builtins_ref__t00_method_t_genN_
;
#ifndef HAS0__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
#define HAS0__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_;
#endif // HAS0__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
;
#ifndef HAS0__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
#define HAS0__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_;
#endif // HAS0__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
;

#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/method.n.o.h"
#undef NLANG_DEFINE_TYPES
struct t00_method_t {
nlang_builtins_i32 x;
}
;

#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/method.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#ifndef HAS2__Ngen_nlang_builtins_ref__t00_method_t_genN_
#define HAS2__Ngen_nlang_builtins_ref__t00_method_t_genN_
struct t00_method_t;
typedef const struct t00_method_t* _Ngen_nlang_builtins_ref__t00_method_t_genN_;
#endif // HAS2__Ngen_nlang_builtins_ref__t00_method_t_genN_
nlang_builtins_i32 t00_method_t_get(_Ngen_nlang_builtins_ref__t00_method_t_genN_ self) __attribute__((__nonnull__(1)));
;
#ifndef HAS2__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
#define HAS2__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_;
#endif // HAS2__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
nlang_builtins_void t00_method_t_set(_Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_ self, nlang_builtins_i32 x) __attribute__((__nonnull__(1)));
;
#ifndef HAS2__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
#define HAS2__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_;
#endif // HAS2__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
nlang_builtins_void t00_method_t_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ self) __attribute__((__nonnull__(1)));
;
void t00_method_t_mk(t00_method_t *_nrtr_r);
;
_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ t00_method_t_new(void);
;
nlang_builtins_void t00_method_t_copy_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ self, _Ngen_nlang_builtins_ref__t00_method_t_genN_ other) __attribute__((__nonnull__(1, 2)));
;

#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/method.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
#ifndef HAS3__Ngen_nlang_builtins_ref__t00_method_t_genN_
#define HAS3__Ngen_nlang_builtins_ref__t00_method_t_genN_
struct t00_method_t;
typedef const struct t00_method_t* _Ngen_nlang_builtins_ref__t00_method_t_genN_;
#endif // HAS3__Ngen_nlang_builtins_ref__t00_method_t_genN_
nlang_builtins_i32 t00_method_t_get(_Ngen_nlang_builtins_ref__t00_method_t_genN_ self) {
#define THIS(x) t00_method_t##x
 {
return (self->x);
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
#define HAS3__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_;
#endif // HAS3__Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_
nlang_builtins_void t00_method_t_set(_Ngen_nlang_builtins_mutable_ref__t00_method_t_genN_ self, nlang_builtins_i32 x) {
#define THIS(x) t00_method_t##x
 {
(self->x) = x;
}
#undef THIS
}
;
#ifndef HAS3__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
#define HAS3__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
struct t00_method_t;
typedef struct t00_method_t* _Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_;
#endif // HAS3__Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_
nlang_builtins_void t00_method_t_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ self) {
#define THIS(x) t00_method_t##x
#undef THIS
}
;
void t00_method_t_mk(t00_method_t *_nrtr_r) {
#define THIS(x) t00_method_t##x
#define r (*_nrtr_r)
#undef r
#undef THIS
}
;
_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ t00_method_t_new(void) {
#define THIS(x) t00_method_t##x
return calloc(1, sizeof(THIS()));
#undef THIS
}
;
nlang_builtins_void t00_method_t_copy_ctor(_Ngen_nlang_builtins_mercurial_ref__t00_method_t_genN_ self, _Ngen_nlang_builtins_ref__t00_method_t_genN_ other) {
#define THIS(x) t00_method_t##x
memcpy(self, other, sizeof(*self));
#undef THIS
}
;

nlang_builtins_i32 _Nmain(void) {
 {
t00_method_t tt= { 0 };
tt.x = (nlang_builtins_i32)0;
;
t00_method_t_set(((&tt)), (nlang_builtins_i32)1);
;
return (t00_method_t_get(((&tt))) - (nlang_builtins_i32)1);
}
}
void t00_method_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_method_Nrunexamples(void) {
}