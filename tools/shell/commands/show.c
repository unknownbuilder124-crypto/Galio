#include "commands/show.h"
#include "kprintf.h"
#include "string.h"
#include "vfs.h"

#define SHOW_BUFFER_SIZE 4096

static void safe_strcat(char *dest, const char *src, u32 max_len) {
    u32 dest_len = strlen(dest);
    u32 copy_len = max_len - dest_len - 1;
    if (copy_len > 0) {
        strncat(dest, src, copy_len);
        dest[max_len - 1] = 0;
    }
}

static void build_filepath(const char *args, const char *current_dir, char *out_path) {
    char filename[256];
    char target_dir[256];

    strncpy(filename, args, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = 0;
    
    char *p = filename;
    while (*p == ' ') p++;
    
    if (*p == 0 || strcmp(p, "show") == 0) {
        out_path[0] = 0;
        return;
    }

    if (p[0] == '/') {
        strncpy(out_path, p, 255);
        out_path[255] = 0;
        return;
    }

    strncpy(target_dir, current_dir, sizeof(target_dir) - 1);
    target_dir[sizeof(target_dir) - 1] = 0;
    int len = strlen(target_dir);
    if (len > 0 && target_dir[len - 1] != '/') {
        safe_strcat(target_dir, "/", sizeof(target_dir));
    }
    safe_strcat(target_dir, p, sizeof(target_dir));
    
    strncpy(out_path, target_dir, 255);
    out_path[255] = 0;
}

u8 shell_show_command(const char *args, const char *current_dir) {
    if (!args || *args == 0) {
        kprintf("[SHOW] Usage: show <filepath>\n");
        kprintf("[SHOW] Example: show file.txt\n");
        return 0;
    }

    char fullpath[256];
    build_filepath(args, current_dir, fullpath);
    
    if (fullpath[0] == 0) {
        kprintf("[SHOW] No file specified\n");
        return 0;
    }

    vfs_entry_t *entry = vfs_find(fullpath);
    if (!entry) {
        kprintf("[SHOW] File not found: %s\n", fullpath);
        return 0;
    }

    if (entry->is_dir) {
        kprintf("[SHOW] %s is a directory, not a file\n", fullpath);
        return 0;
    }

    if (entry->size == 0) {
        kprintf("[SHOW] File is empty: %s\n", fullpath);
        return 1;
    }

    char buffer[SHOW_BUFFER_SIZE];
    u32 bytes_read = vfs_read(fullpath, buffer, SHOW_BUFFER_SIZE - 1);
    buffer[bytes_read] = 0;

    kprintf("\n---[ %s ]---\n", fullpath);
    for (u32 i = 0; i < bytes_read; i++) {
        if (buffer[i] == '\n') kprintf("\n");
        else kprintf("%c", buffer[i]);
    }
    kprintf("\n---[ END ]---\n");

    return 1;
}