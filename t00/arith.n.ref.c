#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/arith.n.o.h"
#undef NLANG_DECLARE_TYPES
#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/arith.n.o.h"
#undef NLANG_DEFINE_TYPES
#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/arith.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/arith.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
nlang_builtins_i32 _Nmain(void) {
 {
nlang_builtins_i32 m = (nlang_builtins_i32)0xffff;
;
nlang_builtins_i32 y = ((m | m) + ((nlang_builtins_i32)0 << (nlang_builtins_u32)1));
;
nlang_builtins_i32 x = ((nlang_builtins_i32)2 & (m | y));
;
return (x - (nlang_builtins_i32)2);
}
}
void t00_arith_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_arith_Nrunexamples(void) {
}