#include "vga.h"
#include "common.h"
#include "cpu.h"

static volatile u16 *vga_buf = (u16*)0xB8000;
static u32 cursor_pos = 0;

static void update_cursor(void) {
    /* Update VGA hardware cursor to the current text position */
    outb(0x3D4, 14);
    outb(0x3D5, (u8)((cursor_pos >> 8) & 0xFF));
    outb(0x3D4, 15);
    outb(0x3D5, (u8)(cursor_pos & 0xFF));
}

static void set_cursor_pos(u32 pos) {
    if (pos >= 80 * 25) {
        pos = 80 * 25 - 1;
    }
    cursor_pos = pos;
    update_cursor();
}

void vga_clear(void) {
    for (u32 i = 0; i < 80*25; ++i) vga_buf[i] = (u16)(' ' | (0x07 << 8));
    cursor_pos = 0;
    update_cursor();
}

void vga_move_cursor(int dx, int dy) {
    u32 row = cursor_pos / 80;
    u32 col = cursor_pos % 80;

    if (dx < 0) {
        u32 delta = (u32)(-dx);
        col = (col < delta) ? 0 : col - delta;
    } else {
        col += (u32)dx;
        if (col >= 80) col = 79;
    }

    if (dy < 0) {
        u32 delta = (u32)(-dy);
        row = (row < delta) ? 0 : row - delta;
    } else {
        row += (u32)dy;
        if (row >= 25) row = 24;
    }

    set_cursor_pos(row * 80 + col);
}

void vga_init(void) {
    vga_clear();
    vga_puts("Galio kernel booting...\n");
}

void vga_putch(char c) {
    if (c == '\n') {
        u32 col = cursor_pos % 80;
        cursor_pos += (80 - col);
    } else {
        vga_buf[cursor_pos++] = (u16)(c | (0x0F << 8));
    }
    if (cursor_pos >= 80*25) {
        /* simple scroll: move everything up one line */
        for (u32 row = 1; row < 25; ++row) {
            for (u32 col = 0; col < 80; ++col) {
                vga_buf[(row-1)*80 + col] = vga_buf[row*80 + col];
            }
        }
        /* clear last line */
        for (u32 col = 0; col < 80; ++col) vga_buf[(25-1)*80 + col] = (u16)(' ' | (0x07 << 8));
        cursor_pos = (25-1)*80;
    }
    update_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putch(*s++);
}
