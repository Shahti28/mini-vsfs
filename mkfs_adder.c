#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h> 

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
#pragma pack(push, 1)

typedef struct {
    // SUPERBLOCK
    // ADDING ALL FIELDS BY THE SPECIFICATION
    
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
    // INODE
    
    uint16_t mode;                // 2
    uint16_t links;               // 2
    uint32_t uid;                 // 4
    uint32_t gid;                 // 4
    uint64_t size_bytes;          // 8
    uint64_t atime;               // 8
    uint64_t mtime;               // 8
    uint64_t ctime;               // 8
    uint32_t direct[DIRECT_MAX];  // 48
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
    // DIRECTORY ENTRY STRUCTURE
    
    uint32_t inode_no;            // 4
    uint8_t  type;                // 1
    char     name[58];            // 58
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


static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(EXIT_FAILURE); }
static uint64_t ceil_div(uint64_t a, uint64_t b){ return (a + b - 1) / b; }

static void set_bit(uint8_t *bmp, uint64_t idx) { bmp[idx >> 3] |= (uint8_t)(1u << (idx & 7u)); }
static int test_bit(const uint8_t *bmp, uint64_t idx) { return (bmp[idx >> 3] >> (idx & 7u)) & 1u; }

static int64_t first_zero_bit(const uint8_t *bmp, uint64_t total_items){
    for (uint64_t i = 0; i < total_items; ++i) if (!test_bit(bmp, i)) return (int64_t)i;
    return -1;
}

static int load_file_bytes(const char *path, uint8_t **out_buf, uint64_t *out_sz){
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long s = ftell(f);
    if (s < 0) { fclose(f); return 0; }
    rewind(f);
    *out_sz = (uint64_t)s;
    *out_buf = malloc(*out_sz ? (size_t)(*out_sz) : 1);
    if (!*out_buf && *out_sz) { fclose(f); return 0; }
    if (*out_sz) {
        size_t r = fread(*out_buf, 1, (size_t)(*out_sz), f);
        if (r != (size_t)(*out_sz)) { fclose(f); free(*out_buf); return 0; }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    crc32_init();
    // DRIVER CODE HERE
    // PARSE CLI PARAMETERS
    // THEN ADDING THE SPECIFIED FILE TO FILE SYSTEM
    // UPDATING THE .IMG FILE ON DISK
    const char *input_img = NULL;
    const char *output_img = NULL;
    const char *file_path = NULL;

    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--input") && i+1<argc) input_img = argv[++i];
        else if (!strcmp(argv[i],"--output") && i+1<argc) output_img = argv[++i];
        else if (!strcmp(argv[i],"--file") && i+1<argc) file_path = argv[++i];
        else {
            fprintf(stderr, "usage: %s --input in.img --output out.img --file <path>\n", argv[0]);
            return 1;
        }
    }
    if (!input_img || !output_img || !file_path) die("missing args");

    // reading input image into memory
    FILE *fi = fopen(input_img, "rb");
    if (!fi) die("cannot open input image");
    if (fseek(fi, 0, SEEK_END) != 0) { fclose(fi); die("seek failed"); }
    long fsz = ftell(fi);
    if (fsz <= 0 || (fsz % BS) != 0) { fclose(fi); die("bad image size"); }
    uint64_t total_blocks = (uint64_t)fsz / BS;
    rewind(fi);

    uint8_t *img = malloc((size_t)fsz);
    if (!img) { fclose(fi); die("malloc"); }
    if (fread(img, BS, (size_t)total_blocks, fi) != (size_t)total_blocks) { fclose(fi); free(img); die("short read"); }
    fclose(fi);

    // loading superblock
    superblock_t sb;
    memcpy(&sb, img + 0 * BS, sizeof(sb));
    if (sb.block_size != BS || sb.total_blocks != total_blocks) { free(img); die("superblock mismatch"); }

    // pointers to bitmaps & inode table
    uint8_t *ibmp = img + sb.inode_bitmap_start * BS;
    uint8_t *dbmp = img + sb.data_bitmap_start * BS;
    uint8_t *itable = img + sb.inode_table_start * BS;

    // loading file bytes
    uint8_t *fbuf = NULL; uint64_t fsize = 0;
    if (!load_file_bytes(file_path, &fbuf, &fsize)) { free(img); die("cannot read file"); }

    uint64_t need_blocks = (fsize == 0) ? 0 : ceil_div(fsize, BS);
    if (need_blocks > DIRECT_MAX) { free(fbuf); free(img); die("file too large"); }

    // finding free inode
    int64_t free_inode_bit = first_zero_bit(ibmp, sb.inode_count);
    if (free_inode_bit < 0) { free(fbuf); free(img); die("no free inode"); }
    uint32_t new_inode_no = (uint32_t)(free_inode_bit + 1);

    // allocating data blocks (first-fit over data_region_blocks)
    uint32_t allocated_blocks[DIRECT_MAX]; memset(allocated_blocks, 0, sizeof(allocated_blocks));
    for (uint64_t k = 0; k < need_blocks; ++k){
        int64_t fb = first_zero_bit(dbmp, sb.data_region_blocks);
        if (fb < 0) { free(fbuf); free(img); die("no free data block"); }
        set_bit(dbmp, (uint64_t)fb);
        allocated_blocks[k] = (uint32_t)(sb.data_region_start + (uint64_t)fb);
    }

    // writing file contents into allocated blocks
    const uint8_t *src = fbuf;
    uint64_t remaining = fsize;
    for (uint64_t k = 0; k < need_blocks; ++k){
        uint8_t *blk = img + (size_t)allocated_blocks[k] * BS;
        uint64_t n = remaining < BS ? remaining : BS;
        if (n) memcpy(blk, src, (size_t)n);
        if (n < BS) memset(blk + n, 0, (size_t)(BS - n));
        src += n;
        remaining -= n;
    }

    // inode struct
    inode_t newino; memset(&newino, 0, sizeof(newino));
    newino.mode = 0100000; // file
    newino.links = 1;
    newino.uid = 0; newino.gid = 0;
    newino.size_bytes = fsize;
    time_t now = time(NULL);
    newino.atime = (uint64_t)now;
    newino.mtime = (uint64_t)now;
    newino.ctime = (uint64_t)now;
    for (int i=0;i<DIRECT_MAX;i++) newino.direct[i] = allocated_blocks[i];
    newino.reserved_0 = newino.reserved_1 = newino.reserved_2 = 0;
    newino.proj_id = 0; newino.uid16_gid16 = 0; newino.xattr_ptr = 0;
    newino.inode_crc = 0;
    inode_crc_finalize(&newino);

    // writing inode into inode table (index = new_inode_no - 1)
    memcpy(itable + (size_t)(new_inode_no - 1) * INODE_SIZE, &newino, sizeof(newino));
    set_bit(ibmp, (uint64_t)(new_inode_no - 1));

    // updating root directory: read root inode
    inode_t root; memcpy(&root, itable + 0 * INODE_SIZE, sizeof(root));
    if ((root.mode & 0040000) == 0) { free(fbuf); free(img); die("root not dir"); }
    if (root.direct[0] == 0) { free(fbuf); free(img); die("root has no data block"); }
    uint8_t *root_dir_blk = img + (size_t)root.direct[0] * BS;

    // finding free dirent slot
    size_t max_entries = BS / sizeof(dirent64_t);
    dirent64_t ent; int placed = 0;
    for (size_t i = 0; i < max_entries; ++i){
        memcpy(&ent, root_dir_blk + i * sizeof(dirent64_t), sizeof(ent));
        if (ent.inode_no == 0){
            memset(&ent, 0, sizeof(ent));
            ent.inode_no = new_inode_no;
            ent.type = 1; // file
            const char *fname = file_path;
            const char *slash = strrchr(file_path, '/');
            if (slash && slash[1]) fname = slash + 1;
            size_t nlen = strlen(fname);
            if (nlen > sizeof(ent.name)) nlen = sizeof(ent.name);
            memcpy(ent.name, fname, nlen);
            dirent_checksum_finalize(&ent);
            memcpy(root_dir_blk + i * sizeof(dirent64_t), &ent, sizeof(ent));
            placed = 1;
            break;
        }
    }
    if (!placed) { free(fbuf); free(img); die("root directory full"); }

    // updating root link count & crc
    root.links += 1;
    inode_crc_finalize(&root);
    memcpy(itable + 0 * INODE_SIZE, &root, sizeof(root));

    // updating sb mtime and checksum
    sb.mtime_epoch = (uint64_t)now;
    superblock_crc_finalize(&sb);
    memcpy(img + 0 * BS, &sb, sizeof(sb));

    // writing new image to disk
    FILE *fo = fopen(output_img, "wb");
    if (!fo) { free(fbuf); free(img); die("cannot open output"); }
    if (fwrite(img, BS, (size_t)total_blocks, fo) != (size_t)total_blocks) { fclose(fo); free(fbuf); free(img); die("short write"); }
    fclose(fo);

    free(fbuf);
    free(img);
    return 0;
}

