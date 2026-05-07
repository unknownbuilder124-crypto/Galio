/* kprintf.c - kernel printf for debugging output */
#include "kprintf.h"
#include "vga.h"
#include "serial.h"
#include <stdarg.h>

void putc(char c) {
    vga_putch(c);
    serial_putc(c);
}

static void prints(const char *s) {
    while (*s) putc(*s++);
}

static void printn(long n, int base) {
    static const char hexdigits[] = "0123456789ABCDEF";
    char buf[32];
    int i = 0;
    
    if (n < 0) {
        putc('-');
        n = -n;
    }
    
    if (n == 0) {
        putc('0');
        return;
    }
    
    while (n > 0) {
        buf[i++] = hexdigits[n % base];
        n /= base;
    }
    
    while (i-- > 0) putc(buf[i]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            ++fmt;
            switch (*fmt) {
                case 'd':
                case 'i':
                    printn(va_arg(ap, int), 10);
                    break;
                case 'u':
                    printn(va_arg(ap, unsigned int), 10);
                    break;
                case 'x':
                case 'X':
                    printn(va_arg(ap, unsigned int), 16);
                    break;
                case 'p':
                    prints("0x");
                    printn(va_arg(ap, unsigned int), 16);
                    break;
                case 's':
                    prints(va_arg(ap, const char *));
                    break;
                case 'c':
                    putc(va_arg(ap, int));
                    break;
                case '%':
                    putc('%');
                    break;
                default:
                    putc('%');
                    putc(*fmt);
                    break;
            }
        } else if (*fmt == '\n') {
            putc('\r');
            putc('\n');
        } else if (*fmt == '\t') {
            putc(' ');
            putc(' ');
            putc(' ');
            putc(' ');
        } else {
            putc(*fmt);
        }
        ++fmt;
    }
    
    va_end(ap);
}
