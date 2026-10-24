// Tiny vsnprintf replacement: %d %i %u %x %X %s %c %%, flags '-' '0',
// width, 'l' modifier. Keeps newlib's printf (and malloc) out of the image.
#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stddef.h>

int fmt_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);

#endif
