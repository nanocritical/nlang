#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/intermediatetyping.n.o.h"
#undef NLANG_DECLARE_TYPES
#ifndef HAS0__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
#define HAS0__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
struct nlang_builtins_error;
typedef const struct nlang_builtins_error* _Ngen_nlang_builtins_ref__nlang_builtins_error_genN_;
#endif // HAS0__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_

#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/intermediatetyping.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/intermediatetyping.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#ifndef HAS2__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
#define HAS2__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
struct nlang_builtins_error;
typedef const struct nlang_builtins_error* _Ngen_nlang_builtins_ref__nlang_builtins_error_genN_;
#endif // HAS2__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
static nlang_builtins_i32 t00_intermediatetyping_noerror(void);

#ifndef HAS2__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
#define HAS2__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
static inline nlang_builtins_i32 _Ngen_nlang_builtins_max__nlang_builtins_i32_genN_(nlang_builtins_i32 a, nlang_builtins_i32 b);
#endif // HAS2__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/intermediatetyping.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
#ifndef HAS3__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
#define HAS3__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
struct nlang_builtins_error;
typedef const struct nlang_builtins_error* _Ngen_nlang_builtins_ref__nlang_builtins_error_genN_;
#endif // HAS3__Ngen_nlang_builtins_ref__nlang_builtins_error_genN_
static nlang_builtins_i32 t00_intermediatetyping_noerror(void) {
 {
nlang_builtins_i32 x = { 0 };
nlang_builtins_error _Ngensym0 = { 0 };
 {
nlang_builtins_error _Ngensym2= { 0 };
_Ngensym2.code = (nlang_builtins_i32)0;
;
_Ngensym0 = _Ngensym2; if (nlang_builtins_error_operator_test(((&_Ngensym2)))) { goto _Ngensym1; };
x = (nlang_builtins_i32)0;
}while (0) {

_Ngensym1: {
(void) (_Ngensym0);
 {
return (nlang_builtins_i32)1;
}
}
}
;
;
return x;
}
}

#ifndef HAS3__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
#define HAS3__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
static inline nlang_builtins_i32 _Ngen_nlang_builtins_max__nlang_builtins_i32_genN_(nlang_builtins_i32 a, nlang_builtins_i32 b) {
 {
( ((a >= b)) ? ( {
return a;
})
 : ( {
return b;
}));
}
}
#endif // HAS3__Ngen_nlang_builtins_max__nlang_builtins_i32_genN_
nlang_builtins_i32 _Nmain(void) {
 {
nlang_builtins_i32 x = ((nlang_builtins_i32)2 + t00_intermediatetyping_noerror());
x = (x + (nlang_builtins_i32)1);
x = (_Ngen_nlang_builtins_max__nlang_builtins_i32_genN_(x, (nlang_builtins_i32)0) + (nlang_builtins_i32)1);
;
return (x - (nlang_builtins_i32)4);
}
}
void t00_intermediatetyping_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_intermediatetyping_Nrunexamples(void) {
}