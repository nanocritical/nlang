#include <string.h>

size_t c99_strlen(const u8 *s) {
  return strlen((const char *) s);
}

void c99_abort(void) {
  return abort();
}
