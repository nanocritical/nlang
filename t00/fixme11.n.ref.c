#include <lib/nlang/runtime.h>
#define NLANG_DECLARE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_TYPES

#define NLANG_DECLARE_TYPES
# include "t00/fixme11.n.o.h"
#undef NLANG_DECLARE_TYPES
#define NLANG_DEFINE_TYPES
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_TYPES

#define NLANG_DEFINE_TYPES
# include "t00/fixme11.n.o.h"
#undef NLANG_DEFINE_TYPES
#define NLANG_DECLARE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS

#define NLANG_DECLARE_FUNCTIONS
# include "t00/fixme11.n.o.h"
#undef NLANG_DECLARE_FUNCTIONS
#define NLANG_DEFINE_FUNCTIONS
# include "lib/nlang/module.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS

#define NLANG_DEFINE_FUNCTIONS
# include "t00/fixme11.n.o.h"
#undef NLANG_DEFINE_FUNCTIONS
nlang_builtins_i32 _Nmain(void) {
 {
;;
}
}
void t00_fixme11_Nrunexamples(void) __attribute__((section(".text.nlang.examples")));
void t00_fixme11_Nrunexamples(void) {
}