#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FS_SIGNATURE "ECS150FS"
#define FS_SIGNATURE_LEN 8
#define BLOCK_SIZE 4096

struct __attribute__((packed)) superblock {
    char signature[8];
    uint16_t total_blocks;
    uint16_t root_index;
    uint16_t data_index;
    uint16_t num_data_blocks;
    uint8_t num_fat_blocks;
    uint8_t padding[4079];
};

static struct superblock sb;
static int disk_opened = 0;

int fs_mount(const char *diskname)
{
    if (disk_opened)
        return -1;

    if (block_disk_open(diskname) < 0)
        return -1;

    uint8_t block[BLOCK_SIZE];
    if (block_read(0, block) < 0)
        return -1;

    memcpy(&sb, block, sizeof(struct superblock));

    if (memcmp(sb.signature, FS_SIGNATURE, FS_SIGNATURE_LEN) != 0) {
        block_disk_close();
        return -1;
    }

    disk_opened = 1;
    return 0;
}

int fs_umount(void)
{
    if (!disk_opened)
        return -1;
    if (block_disk_close() < 0)
        return -1;
    disk_opened = 0;
    memset(&sb, 0, sizeof(sb));
    return 0;
}

int fs_info(void)
{
    if (!disk_opened)
        return -1;
    printf("FS Info:\n");
    printf("total_blk_count=%u\n", sb.total_blocks);
    printf("fat_blk_count=%u\n", sb.num_fat_blocks);
    printf("rdir_blk=%u\n", sb.root_index);
    printf("data_blk=%u\n", sb.data_index);
    printf("data_blk_count=%u\n", sb.num_data_blocks);
    return 0;
}
