#include "fmt.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char* buf;
  size_t size;
  size_t pos;
} out_t;

static void put(out_t* o, char c) {
  if (o->pos + 1 < o->size) o->buf[o->pos] = c;
  o->pos++;
}

static void put_padded(out_t* o, const char* s, size_t len, int width, bool left, char pad) {
  int fill = width > (int) len ? width - (int) len : 0;
  if (!left) {
    // keep the sign in front of zero padding
    if (pad == '0' && len && *s == '-') {
      put(o, *s++);
      len--;
    }
    while (fill--) put(o, pad);
  }
  while (len--) put(o, *s++);
  if (left) {
    while (fill-- > 0) put(o, ' ');
  }
}

int fmt_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
  out_t o = {buf, size, 0};

  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      put(&o, *fmt);
      continue;
    }
    fmt++;

    bool left = false;
    char pad = ' ';
    for (;; fmt++) {
      if (*fmt == '-') left = true;
      else if (*fmt == '0') pad = '0';
      else break;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    bool is_long = false;
    while (*fmt == 'l') {
      is_long = true;
      fmt++;
    }

    char tmp[12];
    char* end = tmp + sizeof(tmp);
    char* p = end;

    switch (*fmt) {
      case 'd':
      case 'i': {
        long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
        unsigned long u = v < 0 ? 0ul - (unsigned long) v : (unsigned long) v;
        do { *--p = (char) ('0' + u % 10); u /= 10; } while (u);
        if (v < 0) *--p = '-';
        put_padded(&o, p, (size_t) (end - p), width, left, left ? ' ' : pad);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        unsigned long u = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
        unsigned base = *fmt == 'u' ? 10u : 16u;
        const char* digits = *fmt == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
        do { *--p = digits[u % base]; u /= base; } while (u);
        put_padded(&o, p, (size_t) (end - p), width, left, left ? ' ' : pad);
        break;
      }
      case 's': {
        const char* s = va_arg(ap, const char*);
        if (!s) s = "(null)";
        size_t len = 0;
        while (s[len]) len++;
        put_padded(&o, s, len, width, left, ' ');
        break;
      }
      case 'c':
        tmp[0] = (char) va_arg(ap, int);
        put_padded(&o, tmp, 1, width, left, ' ');
        break;
      case '%':
        put(&o, '%');
        break;
      case 0:
        fmt--; // format ended after '%'
        break;
      default:
        put(&o, '%');
        put(&o, *fmt);
        break;
    }
  }

  if (o.size) o.buf[o.pos < o.size ? o.pos : o.size - 1] = 0;
  return (int) o.pos;
}
