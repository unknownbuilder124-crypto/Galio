#ifndef VGA_H
#define VGA_H

void vga_init(void);
void vga_puts(const char *s);
void vga_putch(char c);
void vga_clear(void);
void vga_move_cursor(int dx, int dy);

#endif /* VGA_H */
