#include "basl/package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"


/* ── XOR cipher ──────────────────────────────────────────────────── */

static void xor_cipher(unsigned char *data, size_t len,
                        const unsigned char *key, size_t key_len) {
    size_t i;
    if (key_len == 0) return;
    for (i = 0; i < len; i++)
        data[i] ^= key[i % key_len];
}

static int key_is_zero(const unsigned char *key) {
    size_t i;
    for (i = 0; i < BASL_PACKAGE_KEY_LEN; i++)
        if (key[i] != 0) return 0;
    return 1;
}

/* ── Minimal ZIP writer (store-only, no compression) ─────────────── */

static void write_u16_le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void write_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint16_t read_u16_le(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const unsigned char *p) {
    return (uint64_t)read_u32_le(p) | ((uint64_t)read_u32_le(p + 4) << 32);
}

static void write_u64_le(unsigned char *p, uint64_t v) {
    write_u32_le(p, (uint32_t)(v & 0xFFFFFFFF));
    write_u32_le(p + 4, (uint32_t)(v >> 32));
}

/* CRC-32 (used by ZIP format). */
static uint32_t crc32_table[256];
static int crc32_init_done = 0;

static void crc32_init(void) {
    uint32_t i, j, c;
    if (crc32_init_done) return;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320U : 0);
        crc32_table[i] = c;
    }
    crc32_init_done = 1;
}

static uint32_t crc32_compute(const unsigned char *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    size_t i;
    crc32_init();
    for (i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

/* Build a ZIP archive in memory (store-only). */
static unsigned char *zip_build(
    const basl_package_file_t *files, size_t count,
    size_t *out_size
) {
    /* Calculate total size. */
    size_t total = 0;
    size_t cd_size = 0;
    size_t i;
    unsigned char *buf, *p, *cd_start;
    uint32_t *offsets;

    for (i = 0; i < count; i++) {
        total += 30 + files[i].path_length + files[i].data_length; /* local header + data */
        cd_size += 46 + files[i].path_length; /* central directory entry */
    }
    total += cd_size + 22; /* end of central directory */

    buf = (unsigned char *)malloc(total);
    if (buf == NULL) { *out_size = 0; return NULL; }

    offsets = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (offsets == NULL) { free(buf); *out_size = 0; return NULL; }

    p = buf;

    /* Write local file headers + data. */
    for (i = 0; i < count; i++) {
        uint32_t crc = crc32_compute((const unsigned char *)files[i].data, files[i].data_length);
        offsets[i] = (uint32_t)(p - buf);

        write_u32_le(p, 0x04034B50); p += 4;       /* local file header sig */
        write_u16_le(p, 20); p += 2;                /* version needed */
        write_u16_le(p, 0); p += 2;                 /* flags */
        write_u16_le(p, 0); p += 2;                 /* compression: store */
        write_u16_le(p, 0); p += 2;                 /* mod time */
        write_u16_le(p, 0); p += 2;                 /* mod date */
        write_u32_le(p, crc); p += 4;               /* crc-32 */
        write_u32_le(p, (uint32_t)files[i].data_length); p += 4; /* compressed size */
        write_u32_le(p, (uint32_t)files[i].data_length); p += 4; /* uncompressed size */
        write_u16_le(p, (uint16_t)files[i].path_length); p += 2; /* filename length */
        write_u16_le(p, 0); p += 2;                 /* extra field length */
        memcpy(p, files[i].path, files[i].path_length); p += files[i].path_length;
        memcpy(p, files[i].data, files[i].data_length); p += files[i].data_length;
    }

    /* Write central directory. */
    cd_start = p;
    for (i = 0; i < count; i++) {
        uint32_t crc = crc32_compute((const unsigned char *)files[i].data, files[i].data_length);

        write_u32_le(p, 0x02014B50); p += 4;       /* central dir sig */
        write_u16_le(p, 20); p += 2;                /* version made by */
        write_u16_le(p, 20); p += 2;                /* version needed */
        write_u16_le(p, 0); p += 2;                 /* flags */
        write_u16_le(p, 0); p += 2;                 /* compression */
        write_u16_le(p, 0); p += 2;                 /* mod time */
        write_u16_le(p, 0); p += 2;                 /* mod date */
        write_u32_le(p, crc); p += 4;
        write_u32_le(p, (uint32_t)files[i].data_length); p += 4;
        write_u32_le(p, (uint32_t)files[i].data_length); p += 4;
        write_u16_le(p, (uint16_t)files[i].path_length); p += 2;
        write_u16_le(p, 0); p += 2;                 /* extra field length */
        write_u16_le(p, 0); p += 2;                 /* comment length */
        write_u16_le(p, 0); p += 2;                 /* disk number */
        write_u16_le(p, 0); p += 2;                 /* internal attrs */
        write_u32_le(p, 0); p += 4;                 /* external attrs */
        write_u32_le(p, offsets[i]); p += 4;         /* local header offset */
        memcpy(p, files[i].path, files[i].path_length); p += files[i].path_length;
    }

    /* End of central directory. */
    {
        uint32_t cd_total = (uint32_t)(p - cd_start);
        uint32_t cd_off = (uint32_t)(cd_start - buf);
        write_u32_le(p, 0x06054B50); p += 4;        /* signature */
        write_u16_le(p, 0); p += 2;                 /* disk number */
        write_u16_le(p, 0); p += 2;                 /* disk with CD */
        write_u16_le(p, (uint16_t)count); p += 2;   /* entries on disk */
        write_u16_le(p, (uint16_t)count); p += 2;   /* total entries */
        write_u32_le(p, cd_total); p += 4;           /* CD size */
        write_u32_le(p, cd_off); p += 4;             /* CD offset */
        write_u16_le(p, 0); p += 2;                  /* comment length */
    }

    *out_size = (size_t)(p - buf);
    free(offsets);
    return buf;
}

/* ── ZIP reader (minimal, store-only) ────────────────────────────── */

static int zip_read(
    const unsigned char *data, size_t data_len,
    char ***out_paths, char ***out_contents, size_t **out_lengths,
    size_t *out_count
) {
    /* Find end of central directory. */
    size_t eocd_pos;
    uint16_t entry_count;
    uint32_t cd_offset;
    const unsigned char *cd;
    size_t i;
    char **paths, **contents;
    size_t *lengths;

    *out_paths = NULL; *out_contents = NULL; *out_lengths = NULL; *out_count = 0;

    if (data_len < 22) return 0;
    /* Search backward for EOCD signature. */
    for (eocd_pos = data_len - 22; eocd_pos > 0; eocd_pos--) {
        if (read_u32_le(data + eocd_pos) == 0x06054B50) break;
        if (data_len - eocd_pos > 65557) return 0; /* max comment size */
    }
    if (read_u32_le(data + eocd_pos) != 0x06054B50) return 0;

    entry_count = read_u16_le(data + eocd_pos + 10);
    cd_offset = read_u32_le(data + eocd_pos + 16);
    if (cd_offset >= data_len) return 0;

    paths = (char **)calloc(entry_count, sizeof(char *));
    contents = (char **)calloc(entry_count, sizeof(char *));
    lengths = (size_t *)calloc(entry_count, sizeof(size_t));
    if (!paths || !contents || !lengths) {
        free(paths); free(contents); free(lengths);
        return 0;
    }

    cd = data + cd_offset;
    for (i = 0; i < entry_count; i++) {
        uint16_t name_len, extra_len, comment_len;
        uint32_t comp_size, local_offset;
        uint16_t local_name_len, local_extra_len;
        const unsigned char *local;

        if (cd + 46 > data + data_len) goto fail;
        if (read_u32_le(cd) != 0x02014B50) goto fail;

        name_len = read_u16_le(cd + 28);
        extra_len = read_u16_le(cd + 30);
        comment_len = read_u16_le(cd + 32);
        comp_size = read_u32_le(cd + 20);
        local_offset = read_u32_le(cd + 42);

        paths[i] = (char *)malloc(name_len + 1);
        if (!paths[i]) goto fail;
        memcpy(paths[i], cd + 46, name_len);
        paths[i][name_len] = '\0';

        /* Read data from local file header. */
        local = data + local_offset;
        if (local + 30 > data + data_len) goto fail;
        local_name_len = read_u16_le(local + 26);
        local_extra_len = read_u16_le(local + 28);

        contents[i] = (char *)malloc(comp_size + 1);
        if (!contents[i]) goto fail;
        memcpy(contents[i], local + 30 + local_name_len + local_extra_len, comp_size);
        contents[i][comp_size] = '\0';
        lengths[i] = comp_size;

        cd += 46 + name_len + extra_len + comment_len;
    }

    *out_paths = paths;
    *out_contents = contents;
    *out_lengths = lengths;
    *out_count = entry_count;
    return 1;

fail:
    for (i = 0; i < entry_count; i++) {
        free(paths[i]);
        free(contents[i]);
    }
    free(paths); free(contents); free(lengths);
    return 0;
}

/* ── basl_package_build ──────────────────────────────────────────── */

basl_status_t basl_package_build(
    const char *output_path,
    const basl_package_file_t *files,
    size_t file_count,
    const char *key,
    size_t key_length,
    basl_error_t *error
) {
    char self_path[4096];
    char *exe_data = NULL;
    size_t exe_len = 0;
    unsigned char *zip_data = NULL;
    size_t zip_len = 0;
    unsigned char trailer[BASL_PACKAGE_TRAILER_LEN];
    FILE *out = NULL;
    basl_status_t status;

    /* Get path to current executable. */
    status = basl_platform_self_exe(self_path, sizeof(self_path), error);
    if (status != BASL_STATUS_OK) return status;

    /* Read current executable. */
    status = basl_platform_read_file(NULL, self_path, &exe_data, &exe_len, error);
    if (status != BASL_STATUS_OK) return status;

    /* Build ZIP archive. */
    zip_data = zip_build(files, file_count, &zip_len);
    if (zip_data == NULL) {
        free(exe_data);
        if (error) { error->type = BASL_STATUS_OUT_OF_MEMORY; error->value = "zip build failed"; error->length = 16; }
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    /* XOR encrypt if key provided. */
    memset(trailer, 0, sizeof(trailer));
    if (key != NULL && key_length > 0) {
        size_t klen = key_length > BASL_PACKAGE_KEY_LEN ? BASL_PACKAGE_KEY_LEN : key_length;
        memcpy(trailer + 8, key, klen);
        /* Encrypt using the padded key from the trailer so decrypt matches. */
        xor_cipher(zip_data, zip_len, trailer + 8, BASL_PACKAGE_KEY_LEN);
    }

    /* Build trailer: [8 bytes payload len][32 bytes key][8 bytes magic] */
    write_u64_le(trailer, (uint64_t)zip_len);
    memcpy(trailer + 8 + BASL_PACKAGE_KEY_LEN, BASL_PACKAGE_MAGIC, BASL_PACKAGE_MAGIC_LEN);

    /* Write output. */
    out = NULL;
#ifdef _WIN32
    fopen_s(&out, output_path, "wb");
#else
    out = fopen(output_path, "wb");
#endif
    if (out == NULL) {
        free(exe_data); free(zip_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "cannot create output file"; error->length = 25; }
        return BASL_STATUS_INTERNAL;
    }

    if (fwrite(exe_data, 1, exe_len, out) != exe_len ||
        fwrite(zip_data, 1, zip_len, out) != zip_len ||
        fwrite(trailer, 1, BASL_PACKAGE_TRAILER_LEN, out) != BASL_PACKAGE_TRAILER_LEN) {
        fclose(out);
        free(exe_data); free(zip_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "write failed"; error->length = 12; }
        return BASL_STATUS_INTERNAL;
    }

    fclose(out);
    free(exe_data);
    free(zip_data);

    basl_platform_make_executable(output_path, NULL);

    return BASL_STATUS_OK;
}

/* ── basl_package_read ───────────────────────────────────────────── */

static basl_status_t read_bundle_from_file(
    const char *path,
    basl_package_bundle_t *out_bundle,
    basl_error_t *error
) {
    char *file_data = NULL;
    size_t file_len = 0;
    const unsigned char *trailer;
    uint64_t payload_len;
    unsigned char key[BASL_PACKAGE_KEY_LEN];
    unsigned char *zip_data;
    basl_status_t status;

    memset(out_bundle, 0, sizeof(*out_bundle));

    status = basl_platform_read_file(NULL, path, &file_data, &file_len, error);
    if (status != BASL_STATUS_OK) return status;

    if (file_len < BASL_PACKAGE_TRAILER_LEN) {
        free(file_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "no bundle trailer"; error->length = 17; }
        return BASL_STATUS_INTERNAL;
    }

    trailer = (const unsigned char *)file_data + file_len - BASL_PACKAGE_TRAILER_LEN;

    /* Check magic. */
    if (memcmp(trailer + 8 + BASL_PACKAGE_KEY_LEN, BASL_PACKAGE_MAGIC, BASL_PACKAGE_MAGIC_LEN) != 0) {
        free(file_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "not a packaged binary"; error->length = 21; }
        return BASL_STATUS_INTERNAL;
    }

    payload_len = read_u64_le(trailer);
    memcpy(key, trailer + 8, BASL_PACKAGE_KEY_LEN);

    if (payload_len == 0 || payload_len > file_len - BASL_PACKAGE_TRAILER_LEN) {
        free(file_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "invalid payload size"; error->length = 20; }
        return BASL_STATUS_INTERNAL;
    }

    /* Extract and decrypt zip data. */
    zip_data = (unsigned char *)malloc((size_t)payload_len);
    if (zip_data == NULL) {
        free(file_data);
        return BASL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(zip_data, file_data + file_len - BASL_PACKAGE_TRAILER_LEN - payload_len, (size_t)payload_len);
    free(file_data);

    if (!key_is_zero(key)) {
        xor_cipher(zip_data, (size_t)payload_len, key, BASL_PACKAGE_KEY_LEN);
    }

    /* Parse ZIP. */
    if (!zip_read(zip_data, (size_t)payload_len,
                   &out_bundle->paths, &out_bundle->contents,
                   &out_bundle->content_lengths, &out_bundle->file_count)) {
        free(zip_data);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "invalid zip archive"; error->length = 19; }
        return BASL_STATUS_INTERNAL;
    }

    free(zip_data);
    return BASL_STATUS_OK;
}

basl_status_t basl_package_read(
    const char *binary_path,
    basl_package_bundle_t *out_bundle,
    basl_error_t *error
) {
    if (binary_path == NULL || out_bundle == NULL)
        return BASL_STATUS_INVALID_ARGUMENT;
    return read_bundle_from_file(binary_path, out_bundle, error);
}

basl_status_t basl_package_read_self(
    basl_package_bundle_t *out_bundle,
    basl_error_t *error
) {
    char self_path[4096];
    basl_status_t status;
    if (out_bundle == NULL) return BASL_STATUS_INVALID_ARGUMENT;
    status = basl_platform_self_exe(self_path, sizeof(self_path), error);
    if (status != BASL_STATUS_OK) return status;
    return read_bundle_from_file(self_path, out_bundle, error);
}

void basl_package_bundle_free(basl_package_bundle_t *bundle) {
    size_t i;
    if (bundle == NULL) return;
    for (i = 0; i < bundle->file_count; i++) {
        free(bundle->paths[i]);
        free(bundle->contents[i]);
    }
    free(bundle->paths);
    free(bundle->contents);
    free(bundle->content_lengths);
    memset(bundle, 0, sizeof(*bundle));
}
