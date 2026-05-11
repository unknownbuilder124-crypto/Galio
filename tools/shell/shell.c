/* shell.c - Interactive kernel shell (POLLING MODE, NO IRQs) */
#include "shell.h"
#include "vga.h"
#include "kprintf.h"
#include "string.h"
#include <string.h>
#include <stddef.h>
#include "cpu.h"
#include "vfs.h"
#include "auth.h"
#include "commands/new.h"
#include "commands/file.h"
#include "commands/write.h"
#include "commands/show.h"
#include "editor/editor.h"
#define SHELL_BUFFER_SIZE 256
#define HISTORY_SIZE 10
#define HISTORY_BUFFER_SIZE 256
#define DIR_HISTORY_SIZE 32
#define DIR_PATH_SIZE 256
#define ROOT_DIR "/"
#define HOME_DIR "/home"

/* ASCII lookup table for scancodes */
static const u8 ascii_table[] = {
    0,  27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};
static const u8 ascii_table_shift[] = {
    0,  27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};
static u8 shift_pressed = 0;

typedef struct {
    char buffer[SHELL_BUFFER_SIZE];
    u32 len;
} shell_input_t;

typedef struct {
    char history[HISTORY_SIZE][HISTORY_BUFFER_SIZE];
    u32 count;
    u32 index;
} shell_history_t;

typedef struct {
    char stack[DIR_HISTORY_SIZE][DIR_PATH_SIZE];
    u32 sp;
} dir_history_t;

static shell_input_t input;
static shell_history_t history = {0};
static dir_history_t dir_history = {0};
static u8 extended_key = 0;
static char current_dir[256] = HOME_DIR;

static void shell_add_history(const char *cmd) {
    if (input.len == 0) return;
    u32 idx = history.count % HISTORY_SIZE;
    strncpy(history.history[idx], cmd, HISTORY_BUFFER_SIZE - 1);
    history.history[idx][HISTORY_BUFFER_SIZE - 1] = 0;
    if (history.count < HISTORY_SIZE) history.count++;
    history.index = history.count;
}

static void shell_clear_line(void) {
    for (u32 i = 0; i < input.len; i++) {
        vga_putch('\b');
        vga_putch(' ');
        vga_putch('\b');
    }
}

static void shell_print_buffer(void) {
    for (u32 i = 0; i < input.len; i++) {
        vga_putch(input.buffer[i]);
    }
}

static const char *shell_basename(const char *path) {
    const char *base = path;
    while (*path) {
        if (*path == '/') base = path + 1;
        path++;
    }
    return base;
}

u8 shell_dir_command(const char *args, const char *current_dir, u8 replace) {
    if (!args || *args == 0) {
        kprintf("[DIR] Usage: dir <name> [name...]\n");
        return 0;
    }

    char local[512];
    strncpy(local, args, sizeof(local) - 1);
    local[sizeof(local) - 1] = 0;

    char *ptr = local;
    u8 any_success = 0;

    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (*ptr == 0) break;

        char *end = ptr;
        while (*end && *end != ' ') end++;

        char saved_char = *end;
        *end = 0;

        char fullpath[256];
        if (ptr[0] == '/') {
            strncpy(fullpath, ptr, sizeof(fullpath) - 1);
            fullpath[sizeof(fullpath) - 1] = 0;
        } else {
            strncpy(fullpath, current_dir, sizeof(fullpath) - 1);
            fullpath[sizeof(fullpath) - 1] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len - 1] != '/') {
                strncat(fullpath, "/", sizeof(fullpath) - len - 1);
            }
            strncat(fullpath, ptr, sizeof(fullpath) - strlen(fullpath) - 1);
        }

        char parent[256];
        const char *parent_path = fullpath;
        int parent_len = strlen(parent_path) - 1;
        while (parent_len > 0 && parent_path[parent_len] != '/') parent_len--;
        if (parent_len <= 0) {
            strncpy(parent, "/", sizeof(parent) - 1);
            parent[sizeof(parent) - 1] = 0;
        } else {
            if (parent_len >= (int)sizeof(parent)) parent_len = sizeof(parent) - 1;
            memcpy(parent, parent_path, parent_len);
            parent[parent_len] = 0;
        }

        if (!vfs_is_dir(parent)) {
            kprintf("[DIR] Directory does not exist: %s\n", parent);
            *end = saved_char;
            ptr = end + 1;
            continue;
        }

        vfs_entry_t *existing = vfs_find(fullpath);
        if (existing) {
            if (!existing->is_dir) {
                kprintf("[DIR] Path exists and is not a directory: %s\n", fullpath);
            } else if (!replace) {
                kprintf("[DIR] Directory already exists: %s. Use 'rex dir %s' to replace.\n", fullpath, fullpath);
            } else {
                if (vfs_mkdir(fullpath, 1)) any_success = 1;
            }
        } else {
            if (vfs_mkdir(fullpath, replace)) any_success = 1;
        }

        *end = saved_char;
        ptr = end + 1;
    }

    return any_success;
}

/* Parse and execute command */
static void shell_execute_command(void) {
    if (input.len == 0) return;

    input.buffer[input.len] = 0;
    shell_add_history(input.buffer);
    kprintf("\n");

    /* Handle rex (sudo-like) commands */
    if (strncmp(input.buffer, "rex ", 4) == 0) {
        if (!auth_is_authorized()) {
            char password[INPUT_BUFFER_SIZE];
            if (!auth_prompt_password("Password: ", password, INPUT_BUFFER_SIZE) ||
                !auth_verify_password(kernel_auth.username, password)) {
                kprintf("\n[REX] Access denied: Invalid password\n");
                kprintf(" ~[ G ]   < %s >   ", current_dir);
                input.len = 0;
                return;
            }

            auth_authorize();
            kprintf("\n[REX] Password accepted. Privileged mode enabled.\n");
        }

        const char *cmd = input.buffer + 4;
        if (strncmp(cmd, "goto ", 5) == 0) {
            const char *path = cmd + 5;
            if (path[0] == '/') {
                strncpy(current_dir, path, 255);
            } else {
                char fullpath[256];
                strncpy(fullpath, current_dir, 255);
                fullpath[255] = 0;
                int len = strlen(fullpath);
                if (len > 0 && fullpath[len-1] != '/') {
                    strncat(fullpath, "/", 255 - len - 1);
                }
                strncat(fullpath, path, 255 - strlen(fullpath) - 1);
                strncpy(current_dir, fullpath, 255);
            }
            current_dir[255] = 0;
            kprintf("[REX] Changed to: %s\n", current_dir);
        } else if (strncmp(cmd, "file", 4) == 0) {
            const char *file_args = cmd + 4;
            if (*file_args == ' ') file_args++;
            shell_file_command(file_args, current_dir, 1);
        } else if (strncmp(cmd, "new file", 8) == 0) {
            const char *file_args = cmd + 8;
            if (*file_args == ' ') file_args++;
            shell_file_command(file_args, current_dir, 1);
        } else if (strncmp(cmd, "dir", 3) == 0) {
            const char *dir_args = cmd + 3;
            if (*dir_args == ' ') dir_args++;
            shell_dir_command(dir_args, current_dir, 1);
        } else if (strncmp(cmd, "new dir", 7) == 0) {
            const char *dir_args = cmd + 7;
            if (*dir_args == ' ') dir_args++;
            shell_dir_command(dir_args, current_dir, 1);
        } else if (strncmp(cmd, "mkdir", 5) == 0) {
            const char *dir_args = cmd + 5;
            if (*dir_args == ' ') dir_args++;
            shell_dir_command(dir_args, current_dir, 1);
        } else if (strncmp(cmd, "new mkdir", 9) == 0) {
            const char *dir_args = cmd + 9;
            if (*dir_args == ' ') dir_args++;
            shell_dir_command(dir_args, current_dir, 1);
        } else {
            kprintf("[REX] Unknown privileged command: %s\n", cmd);
        }
    } else if (strncmp(input.buffer, "new ", 4) == 0) {
        shell_new_command(input.buffer + 4, current_dir);
    } else if (strcmp(input.buffer, "new") == 0) {
        shell_new_command("", current_dir);
    } else if (strncmp(input.buffer, "file ", 5) == 0) {
        shell_file_command(input.buffer + 5, current_dir, 0);
    } else if (strcmp(input.buffer, "file") == 0) {
        shell_file_command("", current_dir, 0);
    } else if (strncmp(input.buffer, "write ", 6) == 0) {
        shell_write_command(input.buffer + 6, current_dir);
    } else if (strcmp(input.buffer, "write") == 0) {
        shell_write_command("", current_dir);
    } else if (strncmp(input.buffer, "show ", 5) == 0) {
        shell_show_command(input.buffer + 5, current_dir);
    } else if (strcmp(input.buffer, "show") == 0) {
        kprintf("[SHOW] Usage: show <filepath>\n");
        kprintf("[SHOW] Example: show /home/Desktop/file.txt\n");
    } else if (strncmp(input.buffer, "clear", 5) == 0) {
        vga_clear();
        kprintf("                                GSH                                  \n");
        kprintf("                                                                       ");
        kprintf("                                                                       ");
        kprintf("                                                                       ");
        kprintf("\n");
    } else if (strncmp(input.buffer, "help", 4) == 0) {
        kprintf("\n____________________________________________________________________\n");
        kprintf(" |                     GSH  - Available Commands:                   |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  ls       - List directory contents                              |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  dir      - Create directory (usage: dir <name> [name...])        |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  rmdir    - Remove directory (usage: rmdir <path>)               |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  file     - Create file (usage: file <name>[.ext] [name...]   |\n");
        kprintf(" |             file <path/to/name>[.ext] [path...]              |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  new file - Create or replace file (usage: new file              |\n");
        kprintf(" |             <name>[.ext] [name...] or new file                   |\n");
        kprintf(" |             <path/to/name>[.ext] [path...]                     |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  new dir  - Create directory (usage: new dir <name> [name...])    |\n");
        kprintf(" |             Use 'rex new dir' to replace existing directory.     |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  write    - Write/edit file (usage: write <name> [path])         |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  show     - Display file contents (usage: show <filepath>)       |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" | clear    - Clear the screen                                      |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  echo     - Echo text (usage: echo <text>)                       |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  uname    - Show system name                                     |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" | pwd      - Print current directory                               |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  goto     - Change directory (usage: goto <path>)                |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  back     - Go back to a previous dir (usage: back [dirname])    |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  rex      - Privileged command                                   |\n");
        kprintf(" |            gain full access of your device.                      |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  Use UP/DOWN arrows to navigate history                          |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |      *****    NEXT TIME HELP YOURSELF    *****                   |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf("                                                                       ");
        kprintf("                                                                       ");
        kprintf("                                                                       ");
        kprintf("\n");
    } else if (strncmp(input.buffer, "ls", 2) == 0) {
        vfs_listdir(current_dir);
    } else if (strncmp(input.buffer, "dir ", 4) == 0) {
        shell_dir_command(input.buffer + 4, current_dir, 0);
    } else if (strcmp(input.buffer, "dir") == 0) {
        shell_dir_command("", current_dir, 0);
    } else if (strncmp(input.buffer, "mkdir ", 6) == 0) {
        shell_dir_command(input.buffer + 6, current_dir, 0);
    } else if (strcmp(input.buffer, "mkdir") == 0) {
        shell_dir_command("", current_dir, 0);
    } else if (strncmp(input.buffer, "rmdir ", 6) == 0) {
        const char *dirname = input.buffer + 6;
        char fullpath[256];

        if (dirname[0] == '/') {
            strncpy(fullpath, dirname, 255);
            fullpath[255] = 0;
        } else {
            strncpy(fullpath, current_dir, 255);
            fullpath[255] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                strncat(fullpath, "/", 255 - len - 1);
            }
            strncat(fullpath, dirname, 255 - strlen(fullpath) - 1);
        }
        vfs_rmdir(fullpath);
    } else if (strncmp(input.buffer, "pwd", 3) == 0) {
        kprintf("%s\n", current_dir);
    } else if (strncmp(input.buffer, "goto ", 5) == 0) {
        const char *dirname = input.buffer + 5;
        char fullpath[256];

        if (dirname[0] == '/') {
            strncpy(fullpath, dirname, 255);
            fullpath[255] = 0;
        } else {
            strncpy(fullpath, current_dir, 255);
            fullpath[255] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                strncat(fullpath, "/", 255 - len - 1);
            }
            strncat(fullpath, dirname, 255 - strlen(fullpath) - 1);
        }

        if (strcmp(fullpath, ROOT_DIR) == 0) {
            kprintf("Permission denied: use 'rex goto /' to access root\n");
        } else if (vfs_is_dir(fullpath)) {
            if (dir_history.sp < DIR_HISTORY_SIZE) {
                strncpy(dir_history.stack[dir_history.sp], current_dir, DIR_PATH_SIZE - 1);
                dir_history.stack[dir_history.sp][DIR_PATH_SIZE - 1] = 0;
                dir_history.sp++;
            }
            strncpy(current_dir, fullpath, 255);
            current_dir[255] = 0;
        } else {
            kprintf("Directory not found: %s\n", fullpath);
        }
    } else if (strncmp(input.buffer, "back", 4) == 0) {
        const char *target = input.buffer + 4;
        while (*target == ' ') target++;

        if (*target == 0) {
            if (dir_history.sp > 0) {
                dir_history.sp--;
                strncpy(current_dir, dir_history.stack[dir_history.sp], 255);
                current_dir[255] = 0;
            } else {
                kprintf("No previous directory\n");
            }
        } else {
            if (strcmp(target, "/") == 0) {
                kprintf("Permission denied: use 'rex goto /' to access root\n");
            } else {
                u32 found = 0;
                for (u32 i = dir_history.sp; i > 0; i--) {
                    u32 idx = i - 1;
                    if (strcmp(dir_history.stack[idx], target) == 0 ||
                        strcmp(shell_basename(dir_history.stack[idx]), target) == 0) {
                        dir_history.sp = idx;
                        strncpy(current_dir, dir_history.stack[idx], 255);
                        current_dir[255] = 0;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    kprintf("Directory not in history: %s\n", target);
                }
            }
        }
    } else if (strncmp(input.buffer, "echo ", 5) == 0) {
        kprintf("%s\n", input.buffer + 5);
    } else if (strncmp(input.buffer, "uname", 5) == 0) {
        kprintf("Galio v1.0\n");
    } else if (input.len > 0) {
        kprintf("Unknown command: %s\nType 'help' for available commands\n", input.buffer);
    }

    kprintf(" ~[ G ]   < %s >   ", current_dir);
    input.len = 0;
}

/* Poll keyboard for input (no IRQs) */
static void shell_poll_keyboard(void) {
    u8 status = inb(0x64);

    if (status & 0x01) {
        u8 scancode = inb(0x60);

        if (scancode == 0xE0) {
            extended_key = 1;
            return;
        }

        u8 is_pressed = !(scancode & 0x80);
        u8 raw_scancode = scancode & 0x7F;

        if (raw_scancode == 0x2A || raw_scancode == 0x36) {
            shift_pressed = is_pressed;
            return;
        }

        if (!is_pressed) {
            if (extended_key) extended_key = 0;
            return;
        }

        if (extended_key) {
            extended_key = 0;
            if (raw_scancode == 0x48) {
                if (history.index > 0) {
                    history.index--;
                    shell_clear_line();
                    strncpy(input.buffer, history.history[history.index], SHELL_BUFFER_SIZE - 1);
                    input.len = strlen(input.buffer);
                    shell_print_buffer();
                }
                return;
            } else if (raw_scancode == 0x50) {
                if (history.index < history.count - 1) {
                    history.index++;
                    shell_clear_line();
                    strncpy(input.buffer, history.history[history.index], SHELL_BUFFER_SIZE - 1);
                    input.len = strlen(input.buffer);
                    shell_print_buffer();
                } else if (history.index == history.count - 1) {
                    history.index = history.count;
                    shell_clear_line();
                    input.len = 0;
                }
                return;
            } else if (raw_scancode == 0x49) {
                vga_scrollback_up();
                return;
            } else if (raw_scancode == 0x51) {
                vga_scrollback_down();
                return;
            }
            return;
        }

        if (raw_scancode >= sizeof(ascii_table)) return;

        u8 c = shift_pressed ? ascii_table_shift[raw_scancode] : ascii_table[raw_scancode];
        if (c == 0) return;

        if (c == '\b') {
            if (input.len > 0) {
                input.len--;
                vga_putch('\b');
                vga_putch(' ');
                vga_putch('\b');
            }
        } else if (c == '\n') {
            vga_putch('\n');
            shell_execute_command();
        } else if (c == '\t') {
            return;
        } else if (c >= 32 && c < 127) {
            if (input.len < SHELL_BUFFER_SIZE - 1) {
                input.buffer[input.len] = c;
                input.len++;
                vga_putch(c);
            }
        }
    }
}

void shell_run(void) {
    input.len = 0;

    vga_clear();

    strncpy(current_dir, HOME_DIR, sizeof(current_dir) - 1);
    current_dir[sizeof(current_dir) - 1] = 0;

    kprintf("                                 Welcome to GSh                        ");
    kprintf("                                                                       ");
    kprintf("                                                                       ");
    kprintf("                                                                      \n");
    kprintf(" ~[ G ]   < %s >   ", current_dir);

    for (;;) {
        shell_poll_keyboard();
        for (volatile int i = 0; i < 100; i++);
    }
}