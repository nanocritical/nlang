#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/example.n.o.h"
#undef NLANG_DECLARE_TYPES




#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/example.n.o.h"
#undef NLANG_DEFINE_TYPES




#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/example.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
static nlang_builtins_i32 t00_example_foo(nlang_builtins_i32 x);

void t00_example__Nexample0(void) __attribute__((section(".text.nlang.examples")));
void t00_example__Nexample1(void) __attribute__((section(".text.nlang.examples")));
void t00_example__Nexample2(void) __attribute__((section(".text.nlang.examples")));
#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/example.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
static nlang_builtins_i32 t00_example_foo(nlang_builtins_i32 x) {
 {
return (nlang_builtins_i32)0;
}
}

void t00_example__Nexample0(void) {
({ nlang_builtins___example__(((nlang_builtins_i32)0 == t00_example_foo((nlang_builtins_i32)0)));
; });
}
void t00_example__Nexample1(void) {
({ nlang_builtins___example__((t00_example_foo((nlang_builtins_i32)1) == (nlang_builtins_i32)0));
; });
}
void t00_example__Nexample2(void) {
({ nlang_builtins___example__(({ (t00_example_foo((-(nlang_builtins_i32)1)) == (nlang_builtins_i32)0);
; }));
; });
}
nlang_builtins_i32 _Nmain(void) {
 {
return t00_example_foo((nlang_builtins_i32)1);
}
}
void t00_example_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_example_Nrunexamples(void) {
t00_example__Nexample0();
t00_example__Nexample1();
t00_example__Nexample2();
}