// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_minivsfs.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#define BS 4096u               // block size
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0;



#pragma pack(push, 1)
typedef struct {
    // SUPERBLOCK HERE
    // ALL FIELDS AS PROVIDED BY THE SPECIFICATION
    uint32_t magic;               // 4
    uint32_t version;             // 4
    uint32_t block_size;          // 4
    uint64_t total_blocks;        // 8

    uint64_t inode_count;         // 8

    uint64_t inode_bitmap_start;  // 8
    uint64_t inode_bitmap_blocks; // 8

    uint64_t data_bitmap_start;   // 8
    uint64_t data_bitmap_blocks;  // 8

    uint64_t inode_table_start;   // 8
    uint64_t inode_table_blocks;  // 8

    uint64_t data_region_start;   // 8
    uint64_t data_region_blocks;  // 8

    uint64_t root_inode;          // 8

    uint64_t mtime_epoch;         // 8

    uint32_t flags;               // 4
    
    
    uint32_t checksum;            // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    // INODE HERE
    
    uint16_t mode;                // 2
    uint16_t links;               // 2
    uint32_t uid;                 // 4
    uint32_t gid;                 // 4
    uint64_t size_bytes;          // 8
    uint64_t atime;               // 8
    uint64_t mtime;               // 8
    uint64_t ctime;               // 8
    uint32_t direct[12];          // 48
    uint32_t reserved_0;          // 4
    uint32_t reserved_1;          // 4
    uint32_t reserved_2;          // 4
    uint32_t proj_id;             // 4
    uint32_t uid16_gid16;         // 4
    uint64_t xattr_ptr;           // 8

    
    uint64_t inode_crc;   // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    // CREATING DIRECTORY ENTRY STRUCTURE HERE
    uint32_t inode_no;            // 4
    uint8_t  type;                // 1
    char     name[58];            // 58
    // CREATED CORRECTLY FOR THE STATIC_ASSERT ERROR TO BE GONE

    uint8_t  checksum; // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");


// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

// ========================= END OF PROVIDED HELPERS ===========================//

static void die(const char *msg){
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static uint64_t ceil_div(uint64_t a, uint64_t b){ return (a + b - 1) / b; }

static int parse_u64(const char *s, uint64_t *out) {
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (!s[0] || (end && *end)) return 0;
    *out = (uint64_t)v;
    return 1;
}

static void set_bitmap_bit(uint8_t *bmp, uint64_t idx) {
    bmp[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
}



int main(int argc, char **argv) {
    crc32_init();
    // WRITING MY DRIVER CODE HERE
    // PARSEING MY CLI PARAMETERS
    // CREATING FILE SYSTEM WITH A ROOT DIRECTORY
    // THEN SAVING THE DATA INSIDE THE OUTPUT IMAGE
    
    
    // CLI args
    const char *image_name = NULL;
    uint64_t size_kib = 0, inodes = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--image") && i+1 < argc) image_name = argv[++i];
        else if (!strcmp(argv[i], "--size-kib") && i+1 < argc) { if(!parse_u64(argv[++i], &size_kib)) die("bad --size-kib"); }
        else if (!strcmp(argv[i], "--inodes") && i+1 < argc) { if(!parse_u64(argv[++i], &inodes)) die("bad --inodes"); }
        else {
            fprintf(stderr, "usage: %s --image out.img --size-kib <180..4096,mult4> --inodes <128..512>\n", argv[0]);
            return 1;
        }
    }

    if (!image_name) die("missing --image");
    if (size_kib < 180 || size_kib > 4096 || (size_kib % 4) != 0) die("invalid --size-kib (180..4096 and multiple of 4)");
    if (inodes < 128 || inodes > 512) die("invalid --inodes (128..512)");

    uint64_t total_bytes  = size_kib * 1024ull;
    uint64_t total_blocks = total_bytes / BS;
    if (total_blocks < 16) die("image too small");

    // Layout
    uint64_t sb_start = 0;                 // 1 block
    uint64_t ibmp_start = 1;               // 1 block
    uint64_t dbmp_start = 2;               // 1 block

    uint64_t inode_tbl_bytes  = inodes * INODE_SIZE;
    uint64_t inode_tbl_blocks = ceil_div(inode_tbl_bytes, BS);

    uint64_t inode_tbl_start  = 3;
    uint64_t data_region_start = inode_tbl_start + inode_tbl_blocks;
    if (data_region_start >= total_blocks) die("no space for data region");

    uint64_t meta_blocks = 1 + 1 + 1 + inode_tbl_blocks;
    uint64_t data_region_blocks = total_blocks - meta_blocks;

    // Allocate image
    size_t alloc_sz = (size_t)total_blocks * BS;
    void *img = calloc(1, alloc_sz);
    if (!img) die("calloc failed");

    // Superblock
    superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = 0x4D565346u;            // "MVSF"
    sb.version = 1u;
    sb.block_size = BS;
    sb.total_blocks = total_blocks;
    sb.inode_count = inodes;

    sb.inode_bitmap_start = ibmp_start;
    sb.inode_bitmap_blocks = 1;

    sb.data_bitmap_start = dbmp_start;
    sb.data_bitmap_blocks = 1;

    sb.inode_table_start = inode_tbl_start;
    sb.inode_table_blocks = inode_tbl_blocks;

    sb.data_region_start = data_region_start;
    sb.data_region_blocks = data_region_blocks;

    sb.root_inode = ROOT_INO;
    sb.mtime_epoch = (uint64_t)time(NULL);
    sb.flags = 0;
    sb.checksum = 0;
    superblock_crc_finalize(&sb);

    // writing superblock into block 0
    memcpy((uint8_t*)img + sb_start * BS, &sb, sizeof(sb));

    // Inode bitmap (block 1)
    uint8_t ibmp[BS]; memset(ibmp, 0, BS);
    // mark inode #1 allocated (bit index inode-1 = 0)
    set_bitmap_bit(ibmp, 0);
    memcpy((uint8_t*)img + ibmp_start * BS, ibmp, BS);

    // Data bitmap (block 2)
    uint8_t dbmp[BS]; memset(dbmp, 0, BS);
    // reserve first data block for root dir: bit 0
    set_bitmap_bit(dbmp, 0);
    memcpy((uint8_t*)img + dbmp_start * BS, dbmp, BS);

    // Inode table blocks (start at inode_tbl_start)
    // Init root inode and put it at index 0 (inode #1)
    inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    time_t now = time(NULL);

    // mode for directory (octal 0040000)
    root_inode.mode = (uint16_t)0040000;
    root_inode.links = 2;               // '.' and '..'
    root_inode.uid = 0; root_inode.gid = 0;
    root_inode.size_bytes = BS;         // one data block for directory
    root_inode.atime = (uint64_t)now;
    root_inode.mtime = (uint64_t)now;
    root_inode.ctime = (uint64_t)now;

    // First data block for root directory is absolute block data_region_start + 0
    root_inode.direct[0] = (uint32_t)(data_region_start + 0);
    for (int i = 1; i < 12; i++) root_inode.direct[i] = 0;
    root_inode.reserved_0 = root_inode.reserved_1 = root_inode.reserved_2 = 0;
    root_inode.proj_id = 0;
    root_inode.uid16_gid16 = 0;
    root_inode.xattr_ptr = 0;
    root_inode.inode_crc = 0;
    inode_crc_finalize(&root_inode);

    // inode table: copy root inode to first INODE_SIZE bytes inside the inode table region
    uint8_t *itable_ptr = (uint8_t*)img + inode_tbl_start * BS;
    memcpy(itable_ptr + 0 * INODE_SIZE, &root_inode, sizeof(root_inode));
    // remaining inode table bytes are already zero

    // writing inode table blocks back into img (they are already in-place because itable_ptr points into img)
    // (no extra action needed since we wrote directly into img memory)

    // Root directory data block (single block)
    uint8_t dirblk[BS]; memset(dirblk, 0, BS);
    dirent64_t d1, d2;
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));

    d1.inode_no = 1;
    d1.type = 2; // dir
    strncpy(d1.name, ".", sizeof(d1.name));
    dirent_checksum_finalize(&d1);

    d2.inode_no = 1;
    d2.type = 2; // dir
    strncpy(d2.name, "..", sizeof(d2.name));
    dirent_checksum_finalize(&d2);

    // place at entries 0 and 1
    memcpy(dirblk + 0 * sizeof(dirent64_t), &d1, sizeof(dirent64_t));
    memcpy(dirblk + 1 * sizeof(dirent64_t), &d2, sizeof(dirent64_t));

    // writing dir block into data region first block
    memcpy((uint8_t*)img + (size_t)(data_region_start + 0) * BS, dirblk, BS);

    // Finally writing image to disk
    FILE *fp = fopen(image_name, "wb");
    if (!fp) {
        free(img);
        die("cannot open output image");
    }
    size_t written = fwrite(img, BS, (size_t)total_blocks, fp);
    if (written != (size_t)total_blocks) {
        fclose(fp);
        free(img);
        die("short write");
    }
    fclose(fp);
    free(img);
    
    return 0;
}
