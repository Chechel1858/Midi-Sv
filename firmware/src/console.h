// Text console for settings and bring-up. Available on the USB virtual
// COM port and on the debug UART (J22, 115200 8N1). Type "help".
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_poll(void);
void console_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void console_print_banner(void);

#endif
