/* ext2.c - Simplified EXT2 filesystem driver */

#include "ext2.h"
#include "ata.h"
#include "heap.h"
#include "kprintf.h"
#include "string.h"

#define EXT2_ROOT_INODE 2

static ext2_superblock_t superblock;
static ext2_group_desc_t *group_descs = NULL;
static u32 ext2_group_count = 0;
static u32 ext2_group_desc_block = 0;
static u32 ext2_group_desc_blocks = 0;
static u32 block_size = EXT2_BLOCK_SIZE;
static u32 inodes_per_block;
static u32 blocks_per_group;

/* Read a block from disk */
static i32 read_block(u32 block_num, void *buffer) {
    u32 sector = block_num * (block_size / 512);
    u32 sectors = block_size / 512;
    return ata_read_sectors(sector, sectors, buffer);
}

/* Write a block to disk */
static i32 write_block(u32 block_num, const void *buffer) {
    u32 sector = block_num * (block_size / 512);
    u32 sectors = block_size / 512;
    return ata_write_sectors(sector, sectors, buffer);
}

static i32 ext2_write_group_descriptors(void) {
    if (!group_descs || ext2_group_desc_blocks == 0) return -1;
    for (u32 i = 0; i < ext2_group_desc_blocks; i++) {
        if (write_block(ext2_group_desc_block + i, (u8 *)group_descs + i * block_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static i32 ext2_read_group_descriptors(void) {
    if (group_descs) {
        kfree(group_descs);
        group_descs = NULL;
    }
    ext2_group_count = (superblock.blocks_count + blocks_per_group - 1) / blocks_per_group;
    ext2_group_desc_blocks = ((ext2_group_count * sizeof(ext2_group_desc_t)) + block_size - 1) / block_size;
    ext2_group_desc_block = superblock.first_data_block + 1;
    group_descs = kmalloc(ext2_group_desc_blocks * block_size);
    if (!group_descs) {
        kprintf("EXT2: Unable to allocate group descriptor buffer\n");
        return -1;
    }

    for (u32 i = 0; i < ext2_group_desc_blocks; i++) {
        if (read_block(ext2_group_desc_block + i, (u8 *)group_descs + i * block_size) != 0) {
            kprintf("EXT2: Failed to read group descriptor block %u\n", ext2_group_desc_block + i);
            kfree(group_descs);
            group_descs = NULL;
            return -1;
        }
    }
    return 0;
}

i32 ext2_init(void) {
    kprintf("Initializing EXT2 filesystem...\n");

    /* Read superblock (block 1) */
    if (read_block(1, &superblock) != 0) {
        kprintf("EXT2: Failed to read superblock\n");
        return -1;
    }

    if (superblock.magic != EXT2_SIGNATURE) {
        kprintf("EXT2: Invalid signature 0x%x\n", superblock.magic);
        return -1;
    }

    block_size = 1024 << superblock.log_block_size;
    inodes_per_block = block_size / EXT2_INODE_SIZE;
    blocks_per_group = superblock.blocks_per_group;

    if (ext2_read_group_descriptors() != 0) {
        return -1;
    }

    kprintf("EXT2: %u inodes, %u blocks, block size %u, groups %u\n",
            superblock.inodes_count, superblock.blocks_count, block_size, ext2_group_count);

    return 0;
}

i32 ext2_read_inode(u32 inode_num, ext2_inode_t *inode) {
    if (inode_num == 0 || inode_num > superblock.inodes_count) return -1;
    u32 group = (inode_num - 1) / superblock.inodes_per_group;
    u32 index = (inode_num - 1) % superblock.inodes_per_group;
    if (group >= ext2_group_count || !group_descs) return -1;

    u32 inode_table_block = group_descs[group].bg_inode_table + (index / inodes_per_block);
    u8 buffer[block_size];
    if (read_block(inode_table_block, buffer) != 0) {
        return -1;
    }

    memcpy(inode, buffer + (index % inodes_per_block) * EXT2_INODE_SIZE, sizeof(ext2_inode_t));
    return 0;
}

i32 ext2_read_block(u32 block_num, void *buffer) {
    return read_block(block_num, buffer);
}

i32 ext2_write_block(u32 block_num, const void *buffer) {
    return write_block(block_num, buffer);
}

u32 ext2_find_inode(const char *path) {
    if (!path || path[0] != '/') return 0;

    u32 current_inode = EXT2_ROOT_INODE;
    const char *p = path + 1;

    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;

        if (!*p) break;

        /* Get next component */
        const char *end = p;
        while (*end && *end != '/') end++;

        char name[256];
        u32 len = end - p;
        memcpy(name, p, len);
        name[len] = 0;

        /* Read current inode */
        ext2_inode_t inode;
        if (ext2_read_inode(current_inode, &inode) != 0) {
            return 0;
        }

        /* Search directory */
        u8 buffer[block_size];
        u32 found = 0;

        for (u32 i = 0; i < 12 && inode.block[i]; i++) {  /* Direct blocks only */
            if (read_block(inode.block[i], buffer) != 0) continue;

            ext2_dirent_t *dent = (ext2_dirent_t *)buffer;
            while ((u8 *)dent < buffer + block_size) {
                if (dent->inode && strncmp(dent->name, name, dent->name_len) == 0) {
                    current_inode = dent->inode;
                    found = 1;
                    break;
                }
                dent = (ext2_dirent_t *)((u8 *)dent + dent->rec_len);
            }
            if (found) break;
        }

        if (!found) return 0;
        p = end;
    }

    return current_inode;
}

/* Write inode to disk */
i32 ext2_write_inode(u32 inode_num, ext2_inode_t *inode) {
    if (!inode || inode_num == 0 || inode_num > superblock.inodes_count) return -1;
    u32 group = (inode_num - 1) / superblock.inodes_per_group;
    u32 index = (inode_num - 1) % superblock.inodes_per_group;
    if (group >= ext2_group_count || !group_descs) return -1;

    u32 inode_table_block = group_descs[group].bg_inode_table + (index / inodes_per_block);
    u8 buffer[block_size];
    if (read_block(inode_table_block, buffer) != 0) {
        return -1;
    }

    u32 offset = (index % inodes_per_block) * EXT2_INODE_SIZE;
    memcpy(buffer + offset, inode, sizeof(ext2_inode_t));

    if (write_block(inode_table_block, buffer) != 0) {
        return -1;
    }

    return 0;
}

/* Allocate a free block */
i32 ext2_alloc_block(void) {
    if (superblock.free_blocks_count == 0 || !group_descs) return -1;

    for (u32 group = 0; group < ext2_group_count; group++) {
        u32 bitmap_block = group_descs[group].bg_block_bitmap;
        u8 bitmap[block_size];

        if (read_block(bitmap_block, bitmap) != 0) {
            return -1;
        }

        for (u32 i = 0; i < block_size; i++) {
            if (bitmap[i] != 0xFF) {
                for (u32 j = 0; j < 8; j++) {
                    if (!(bitmap[i] & (1 << j))) {
                        u32 block_num = group * blocks_per_group + i * 8 + j;
                        if (block_num >= superblock.blocks_count) continue;

                        bitmap[i] |= (1 << j);
                        if (write_block(bitmap_block, bitmap) != 0) {
                            return -1;
                        }

                        superblock.free_blocks_count--;
                        group_descs[group].bg_free_blocks_count--;
                        if (write_block(1, &superblock) != 0) {
                            return -1;
                        }
                        if (ext2_write_group_descriptors() != 0) {
                            return -1;
                        }

                        return block_num;
                    }
                }
            }
        }
    }

    return -1;
}

/* Allocate a free inode */
i32 ext2_alloc_inode(void) {
    if (superblock.free_inodes_count == 0 || !group_descs) return -1;

    for (u32 group = 0; group < ext2_group_count; group++) {
        u32 bitmap_block = group_descs[group].bg_inode_bitmap;
        u8 bitmap[block_size];

        if (read_block(bitmap_block, bitmap) != 0) {
            return -1;
        }

        for (u32 i = 0; i < block_size; i++) {
            if (bitmap[i] != 0xFF) {
                for (u32 j = 0; j < 8; j++) {
                    if (!(bitmap[i] & (1 << j))) {
                        u32 inode_num = group * superblock.inodes_per_group + i * 8 + j + 1;
                        if (inode_num > superblock.inodes_count) continue;

                        bitmap[i] |= (1 << j);
                        if (write_block(bitmap_block, bitmap) != 0) {
                            return -1;
                        }

                        superblock.free_inodes_count--;
                        group_descs[group].bg_free_inodes_count--;
                        if (write_block(1, &superblock) != 0) {
                            return -1;
                        }
                        if (ext2_write_group_descriptors() != 0) {
                            return -1;
                        }

                        ext2_inode_t new_inode;
                        memset(&new_inode, 0, sizeof(ext2_inode_t));
                        if (ext2_write_inode(inode_num, &new_inode) != 0) {
                            return -1;
                        }

                        return inode_num;
                    }
                }
            }
        }
    }

    return -1;
}

/* Add directory entry to a directory */
i32 ext2_add_directory_entry(u32 dir_inode, const char *name, u32 child_inode) {
    if (!name || dir_inode == 0 || child_inode == 0) return -1;

    ext2_inode_t dir_ino;
    if (ext2_read_inode(dir_inode, &dir_ino) != 0) {
        return -1;
    }

    u32 name_len = 0;
    while (name[name_len]) name_len++;

    u8 buffer[block_size];

    /* Try to find space in existing blocks */
    for (u32 i = 0; i < 12 && dir_ino.block[i]; i++) {
        if (read_block(dir_ino.block[i], buffer) != 0) continue;

        u8 *pos = buffer;
        while ((u32)(pos - buffer) < block_size) {
            ext2_dirent_t *dent = (ext2_dirent_t *)pos;
            if (dent->rec_len == 0) break;

            u32 used_len = 8 + dent->name_len;
            if (used_len % 4) used_len += 4 - (used_len % 4);

            u32 remaining = dent->rec_len - used_len;
            if (remaining >= 8 + name_len) {
                /* Found space */
                u8 *new_entry = pos + used_len;
                ext2_dirent_t *new_dent = (ext2_dirent_t *)new_entry;

                new_dent->inode = child_inode;
                new_dent->rec_len = remaining;
                new_dent->name_len = name_len;
                new_dent->file_type = 0;
                memcpy(new_dent->name, name, name_len);

                /* Update original entry's rec_len */
                dent->rec_len = used_len;

                if (write_block(dir_ino.block[i], buffer) != 0) {
                    return -1;
                }

                return 0;
            }

            pos += dent->rec_len;
        }
    }

    /* Need to allocate a new block */
    i32 new_block = ext2_alloc_block();
    if (new_block < 0) return -1;

    /* Create first entry in new block */
    memset(buffer, 0, block_size);
    ext2_dirent_t *dent = (ext2_dirent_t *)buffer;
    dent->inode = child_inode;
    dent->rec_len = block_size;
    dent->name_len = name_len;
    dent->file_type = 0;
    memcpy(dent->name, name, name_len);

    if (write_block(new_block, buffer) != 0) {
        return -1;
    }

    /* Add block reference to inode */
    for (u32 i = 0; i < 12; i++) {
        if (dir_ino.block[i] == 0) {
            dir_ino.block[i] = new_block;
            break;
        }
    }

    dir_ino.size += block_size;
    dir_ino.blocks += block_size / 512;

    if (ext2_write_inode(dir_inode, &dir_ino) != 0) {
        return -1;
    }

    return 0;
}

/* Update inode size */
i32 ext2_update_inode_size(u32 inode_num, u32 new_size) {
    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) {
        return -1;
    }

    inode.size = new_size;
    return ext2_write_inode(inode_num, &inode);
}

/* Write data to inode */
i32 ext2_write_data(u32 inode_num, const void *buffer, u32 size) {
    if (!buffer || size == 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) {
        return -1;
    }

    u32 written = 0;
    u32 block_offset = 0;
    u8 *data = (u8 *)buffer;

    while (written < size) {
        u32 to_write = size - written;
        if (to_write > block_size) to_write = block_size;

        i32 block_num;
        if (inode.block[block_offset] == 0) {
            /* Allocate new block */
            block_num = ext2_alloc_block();
            if (block_num < 0) return -1;
            inode.block[block_offset] = block_num;
        } else {
            block_num = inode.block[block_offset];
        }

        if (write_block(block_num, data + written) != 0) {
            return -1;
        }

        written += to_write;
        block_offset++;

        if (block_offset >= 12) break;  /* Direct blocks only */
    }

    inode.size = written;
    inode.blocks = (written + 511) / 512;

    if (ext2_write_inode(inode_num, &inode) != 0) {
        return -1;
    }

    return written;
}

/* Create a new file */
i32 ext2_create_file(const char *path, u32 mode) {
    (void)mode;  /* Using default mode 0x81A4 */

    if (!path || path[0] != '/') return -1;

    /* Find parent directory */
    char parent_path[256];
    char filename[256];
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash == path) {
        parent_path[0] = '/';
        parent_path[1] = 0;
        strcpy(filename, path + 1);
    } else {
        u32 parent_len = last_slash - path;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = 0;
        strcpy(filename, last_slash + 1);
    }

    u32 parent_inode_num = ext2_find_inode(parent_path);
    if (parent_inode_num == 0) return -1;

    /* Allocate new inode */
    i32 new_inode_num = ext2_alloc_inode();
    if (new_inode_num < 0) return -1;

    /* Initialize inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = 0x81A4;  /* Regular file, 644 permissions */
    new_inode.size = 0;
    new_inode.blocks = 0;
    new_inode.links_count = 1;

    if (ext2_write_inode(new_inode_num, &new_inode) != 0) {
        return -1;
    }

    /* Add directory entry */
    if (ext2_add_directory_entry(parent_inode_num, filename, new_inode_num) != 0) {
        return -1;
    }

    return new_inode_num;
}

/* Create a new directory */
i32 ext2_create_directory(const char *path, u32 mode) {
    (void)mode;  /* Using default mode */

    if (!path || path[0] != '/') return -1;

    /* Find parent directory */
    char parent_path[256];
    char dirname[256];
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash == path) {
        parent_path[0] = '/';
        parent_path[1] = 0;
        strcpy(dirname, path + 1);
    } else {
        u32 parent_len = last_slash - path;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = 0;
        strcpy(dirname, last_slash + 1);
    }

    u32 parent_inode_num = ext2_find_inode(parent_path);
    if (parent_inode_num == 0) return -1;

    /* Allocate new inode */
    i32 new_inode_num = ext2_alloc_inode();
    if (new_inode_num < 0) return -1;

    /* Allocate block for directory entries */
    i32 dir_block = ext2_alloc_block();
    if (dir_block < 0) return -1;

    /* Initialize directory inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = 0x41ED;  /* Directory, 755 permissions */
    new_inode.size = block_size;
    new_inode.blocks = block_size / 512;
    new_inode.links_count = 2;
    new_inode.block[0] = dir_block;

    if (ext2_write_inode(new_inode_num, &new_inode) != 0) {
        return -1;
    }

    /* Create '.' and '..' entries in new directory */
    u8 dir_buffer[block_size];
    memset(dir_buffer, 0, block_size);

    /* '.' entry */
    ext2_dirent_t *dot = (ext2_dirent_t *)dir_buffer;
    dot->inode = new_inode_num;
    dot->rec_len = 12;  /* 8 + 4 for name */
    dot->name_len = 1;
    dot->file_type = 2;  /* Directory */
    dot->name[0] = '.';

    /* '..' entry */
    ext2_dirent_t *dotdot = (ext2_dirent_t *)((u8 *)dot + 12);
    dotdot->inode = parent_inode_num;
    dotdot->rec_len = block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = 2;  /* Directory */
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    if (write_block(dir_block, dir_buffer) != 0) {
        return -1;
    }

    /* Add directory entry to parent */
    if (ext2_add_directory_entry(parent_inode_num, dirname, new_inode_num) != 0) {
        return -1;
    }

    return new_inode_num;
}
