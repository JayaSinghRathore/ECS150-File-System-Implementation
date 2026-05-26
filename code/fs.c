#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FS_SIGNATURE       "ECS150FS"
#define FS_SIGNATURE_LEN   8
#define FAT_EOC            0xFFFF

struct __attribute__((packed)) superblock {
    char     signature[FS_SIGNATURE_LEN];
    uint16_t total_blocks;
    uint16_t root_index;
    uint16_t data_index;
    uint16_t data_count;
    uint8_t  fat_blocks;
    uint8_t  padding[4079];
};

struct __attribute__((packed)) root_entry {
    char     filename[FS_FILENAME_LEN];
    uint32_t size;
    uint16_t first_data_index;
    uint8_t  padding[10];
};

struct fd_entry {
    int     used;
    int     root_index;
    size_t  offset;
};

static struct superblock   sb;
static uint16_t           *fat = NULL;
static struct root_entry   root[FS_FILE_MAX_COUNT];
static struct fd_entry     fd_table[FS_OPEN_MAX_COUNT];
static int                 fs_mounted = 0;

static int find_root(const char *filename) {
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (root[i].filename[0] != '\0' &&
            strncmp(root[i].filename, filename, FS_FILENAME_LEN) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int find_free_root(void) {
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (root[i].filename[0] == '\0') {
            return i;
        }
    }
    return -1;
}

static uint16_t find_free_fat(void) {
    for (uint16_t i = 1; i < sb.data_count; i++) {
        if (fat[i] == 0) {
            return i;
        }
    }
    return 0;
}

static int find_free_fd(void) {
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (!fd_table[i].used) {
            return i;
        }
    }
    return -1;
}

static int read_superblock(void) {
    uint8_t buf[BLOCK_SIZE];
    if (block_read(0, buf) < 0) {
        return -1;
    }
    memcpy(&sb, buf, sizeof(struct superblock));
    if (memcmp(sb.signature, FS_SIGNATURE, FS_SIGNATURE_LEN) != 0) {
        return -1;
    }
    if (sb.total_blocks != (uint16_t)block_disk_count()) {
        return -1;
    }
    return 0;
}

static int load_fat(void) {
    size_t entry_bytes = (size_t)sb.data_count * sizeof(uint16_t);
    fat = calloc(sb.data_count, sizeof(uint16_t));
    if (fat == NULL) {
        return -1;
    }
    for (int i = 0; i < sb.fat_blocks; i++) {
        uint8_t buf[BLOCK_SIZE];
        if (block_read(1 + i, buf) < 0) {
            free(fat);
            fat = NULL;
            return -1;
        }
        size_t offset = (size_t)i * BLOCK_SIZE;
        size_t to_copy = BLOCK_SIZE;
        if (offset + to_copy > entry_bytes) {
            to_copy = entry_bytes - offset;
        }
        memcpy(((uint8_t *)fat) + offset, buf, to_copy);
    }
    return 0;
}

static int write_fat(void) {
    size_t entry_bytes = (size_t)sb.data_count * sizeof(uint16_t);
    for (int i = 0; i < sb.fat_blocks; i++) {
        uint8_t buf[BLOCK_SIZE];
        memset(buf, 0, BLOCK_SIZE);
        size_t offset = (size_t)i * BLOCK_SIZE;
        size_t to_copy = BLOCK_SIZE;
        if (offset + to_copy > entry_bytes) {
            to_copy = entry_bytes - offset;
        }
        memcpy(buf, ((uint8_t *)fat) + offset, to_copy);
        if (block_write(1 + i, buf) < 0) {
            return -1;
        }
    }
    return 0;
}

static int load_root_dir(void) {
    if (block_read(sb.root_index, (uint8_t *)root) < 0) {
        return -1;
    }
    return 0;
}

static int write_root_dir(void) {
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, root, sizeof(root));
    if (block_write(sb.root_index, buf) < 0) {
        return -1;
    }
    return 0;
}

int fs_mount(const char *diskname) {
    if (fs_mounted) {
        return -1;
    }
    if (block_disk_open(diskname) < 0) {
        return -1;
    }
    if (read_superblock() < 0) {
        block_disk_close();
        return -1;
    }
    if (load_fat() < 0) {
        block_disk_close();
        return -1;
    }
    if (load_root_dir() < 0) {
        free(fat);
        fat = NULL;
        block_disk_close();
        return -1;
    }
    memset(fd_table, 0, sizeof(fd_table));
    fs_mounted = 1;
    return 0;
}

int fs_umount(void) {
    if (!fs_mounted) {
        return -1;
    }
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fd_table[i].used) {
            return -1;
        }
    }
    if (write_fat() < 0) {
        return -1;
    }
    if (write_root_dir() < 0) {
        return -1;
    }
    free(fat);
    fat = NULL;
    fs_mounted = 0;
    if (block_disk_close() < 0) {
        return -1;
    }
    return 0;
}

int fs_info(void) {
    if (!fs_mounted || fat == NULL) {
        return -1;
    }
    printf("FS Info:\n");
    printf("total_blk_count=%u\n", sb.total_blocks);
    printf("fat_blk_count=%u\n", sb.fat_blocks);
    printf("rdir_blk=%u\n", sb.root_index);
    printf("data_blk=%u\n", sb.data_index);
    printf("data_blk_count=%u\n", sb.data_count);
    size_t free_fat = 0;
    for (uint16_t i = 1; i < sb.data_count; i++) {
        if (fat[i] == 0) {
            free_fat++;
        }
    }
    printf("fat_free_ratio=%zu/%u\n", free_fat, sb.data_count);
    size_t free_root = 0;
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (root[i].filename[0] == '\0') {
            free_root++;
        }
    }
    printf("rdir_free_ratio=%zu/%d\n", free_root, FS_FILE_MAX_COUNT);
    return 0;
}

int fs_create(const char *filename) {
    if (!fs_mounted || filename == NULL) {
        return -1;
    }
    size_t len = strnlen(filename, FS_FILENAME_LEN);
    if (len == 0 || len >= FS_FILENAME_LEN) {
        return -1;
    }
    if (find_root(filename) != -1) {
        return -1;
    }
    int idx = find_free_root();
    if (idx < 0) {
        return -1;
    }
    memset(&root[idx], 0, sizeof(struct root_entry));
    strncpy(root[idx].filename, filename, FS_FILENAME_LEN - 1);
    root[idx].filename[FS_FILENAME_LEN - 1] = '\0';
    root[idx].size = 0;
    root[idx].first_data_index = FAT_EOC;
    return 0;
}

int fs_delete(const char *filename) {
    if (!fs_mounted || filename == NULL) {
        return -1;
    }
    int idx = find_root(filename);
    if (idx < 0) {
        return -1;
    }
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fd_table[i].used && fd_table[i].root_index == idx) {
            return -1;
        }
    }
    uint16_t curr = root[idx].first_data_index;
    while (curr != FAT_EOC && curr != 0) {
        uint16_t next = fat[curr];
        fat[curr] = 0;
        curr = next;
    }
    memset(&root[idx], 0, sizeof(struct root_entry));
    return 0;
}

int fs_ls(void) {
    if (!fs_mounted) {
        return -1;
    }
    printf("FS Ls:\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (root[i].filename[0] != '\0') {
            printf("file: %s, size: %u, data_blk: %u\n",
                   root[i].filename,
                   root[i].size,
                   root[i].first_data_index);
        }
    }
    return 0;
}

int fs_open(const char *filename) {
    if (!fs_mounted || filename == NULL) {
        return -1;
    }
    int rdx = find_root(filename);
    if (rdx < 0) {
        return -1;
    }
    int fd = find_free_fd();
    if (fd < 0) {
        return -1;
    }
    fd_table[fd].used = 1;
    fd_table[fd].root_index = rdx;
    fd_table[fd].offset = 0;
    return fd;
}

int fs_close(int fd) {
    if (!fs_mounted
        || fd < 0
        || fd >= FS_OPEN_MAX_COUNT
        || !fd_table[fd].used)
    {
        return -1;
    }
    fd_table[fd].used = 0;
    return 0;
}

int fs_stat(int fd) {
    if (!fs_mounted
        || fd < 0
        || fd >= FS_OPEN_MAX_COUNT
        || !fd_table[fd].used)
    {
        return -1;
    }
    int rdx = fd_table[fd].root_index;
    return (int)root[rdx].size;
}

int fs_lseek(int fd, size_t offset) {
    if (!fs_mounted
        || fd < 0
        || fd >= FS_OPEN_MAX_COUNT
        || !fd_table[fd].used)
    {
        return -1;
    }
    int rdx = fd_table[fd].root_index;
    if (offset > root[rdx].size) {
        return -1;
    }
    fd_table[fd].offset = offset;
    return 0;
}

int fs_read(int fd, void *buf, size_t count) {
    if (!fs_mounted
        || fd < 0
        || fd >= FS_OPEN_MAX_COUNT
        || !fd_table[fd].used
        || buf == NULL)
    {
        return -1;
    }
    int rdx = fd_table[fd].root_index;
    struct root_entry *re = &root[rdx];
    size_t file_off = fd_table[fd].offset;
    if (file_off >= re->size) {
        return 0;
    }
    if (count > re->size - file_off) {
        count = re->size - file_off;
    }
    size_t bytes_read = 0;
    size_t skip_blocks = file_off / BLOCK_SIZE;
    size_t block_off = file_off % BLOCK_SIZE;
    uint16_t curr = re->first_data_index;
    for (size_t i = 0; i < skip_blocks && curr != FAT_EOC; i++) {
        curr = fat[curr];
    }
    while (bytes_read < count && curr != FAT_EOC) {
        uint8_t bounce[BLOCK_SIZE];
        if (block_read(sb.data_index + curr, bounce) < 0) {
            break;
        }
        block_off = (file_off + bytes_read) % BLOCK_SIZE;
        size_t can_copy = BLOCK_SIZE - block_off;
        if (can_copy > (count - bytes_read)) {
            can_copy = count - bytes_read;
        }
        memcpy(((uint8_t *)buf) + bytes_read, bounce + block_off, can_copy);
        bytes_read += can_copy;
        curr = fat[curr];
    }
    fd_table[fd].offset += bytes_read;
    return (int)bytes_read;
}

int fs_write(int fd, void *buf, size_t count) {
    if (!fs_mounted
        || fd < 0
        || fd >= FS_OPEN_MAX_COUNT
        || !fd_table[fd].used
        || buf == NULL)
    {
        return -1;
    }
    int rdx = fd_table[fd].root_index;
    struct root_entry *re = &root[rdx];
    size_t file_off = fd_table[fd].offset;
    size_t new_end = file_off + count;
    if (re->first_data_index == FAT_EOC && count > 0) {
        uint16_t nb = find_free_fat();
        if (nb == 0) {
            return 0;
        }
        fat[nb] = FAT_EOC;
        re->first_data_index = nb;
    }
    uint16_t curr = re->first_data_index;
    uint16_t prev = FAT_EOC;
    size_t existing_blocks = 0;
    if (curr != FAT_EOC) {
        existing_blocks = 1;
        while (fat[curr] != FAT_EOC) {
            curr = fat[curr];
            existing_blocks++;
        }
        prev = curr;
    }
    size_t blocks_needed = (new_end + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocks_needed > existing_blocks) {
        size_t to_alloc = blocks_needed - existing_blocks;
        if (existing_blocks == 0) {
            curr = re->first_data_index;
            prev = curr;
            existing_blocks = 1;
        } else {
            curr = re->first_data_index;
            while (fat[curr] != FAT_EOC) {
                curr = fat[curr];
            }
            prev = curr;
        }
        for (size_t i = 0; i < to_alloc; i++) {
            uint16_t nb = find_free_fat();
            if (nb == 0) {
                break;
            }
            fat[nb] = FAT_EOC;
            fat[prev] = nb;
            prev = nb;
        }
    }
    curr = re->first_data_index;
    prev = FAT_EOC;
    size_t skip_blocks = file_off / BLOCK_SIZE;
    for (size_t i = 0; i < skip_blocks; i++) {
        prev = curr;
        curr = fat[curr];
        if (curr == FAT_EOC) {
            uint16_t nb = find_free_fat();
            if (nb == 0) {
                break;
            }
            fat[nb] = FAT_EOC;
            fat[prev] = nb;
            curr = nb;
        }
    }
    size_t bytes_written = 0;
    while (bytes_written < count && curr != FAT_EOC) {
        size_t block_off = (file_off + bytes_written) % BLOCK_SIZE;
        uint8_t bounce[BLOCK_SIZE];
        if (block_off != 0 || (count - bytes_written) < BLOCK_SIZE) {
            if (block_read(sb.data_index + curr, bounce) < 0) {
                break;
            }
        } else {
            memset(bounce, 0, BLOCK_SIZE);
        }
        size_t can_copy = BLOCK_SIZE - block_off;
        if (can_copy > (count - bytes_written)) {
            can_copy = count - bytes_written;
        }
        memcpy(bounce + block_off, ((uint8_t *)buf) + bytes_written, can_copy);
        if (block_write(sb.data_index + curr, bounce) < 0) {
            break;
        }
        bytes_written += can_copy;
        prev = curr;
        curr = fat[curr];
    }
    fd_table[fd].offset += bytes_written;
    if (fd_table[fd].offset > re->size) {
        re->size = fd_table[fd].offset;
    }
    return (int)bytes_written;
}
