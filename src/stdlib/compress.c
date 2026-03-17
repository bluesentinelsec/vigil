/* BASL standard library: compress module.
 *
 * Compression and decompression using deflate, zlib, gzip formats.
 * Uses miniz library (MIT license).
 */
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/stdlib.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"
#include "miniz.h"
#include "lz4.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *get_bytes_data(basl_value_t v, size_t *out_len) {
    if (basl_nanbox_is_object(v)) {
        const basl_object_t *obj = (const basl_object_t *)basl_nanbox_decode_ptr(v);
        if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
            *out_len = basl_string_object_length(obj);
            return basl_string_object_c_str(obj);
        }
    }
    *out_len = 0;
    return NULL;
}

static basl_status_t push_bytes(basl_vm_t *vm, const void *data, size_t len, basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), (const char *)data, len, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

static basl_status_t push_empty_bytes(basl_vm_t *vm, basl_error_t *error) {
    return push_bytes(vm, "", 0, error);
}

/* ── Zlib ────────────────────────────────────────────────────────── */

static basl_status_t zlib_compress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong dst_len;
    unsigned char *dst;
    int status;
    basl_status_t ret;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    dst_len = mz_compressBound((mz_ulong)src_len);
    dst = (unsigned char *)malloc(dst_len);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    status = mz_compress(dst, &dst_len, (const unsigned char *)src, (mz_ulong)src_len);
    if (status != MZ_OK) {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, dst, dst_len, error);
    free(dst);
    return ret;
}

static basl_status_t zlib_decompress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong dst_len;
    unsigned char *dst;
    int status;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    /* Start with 4x source size, grow if needed */
    dst_len = (mz_ulong)(src_len * 4);
    if (dst_len < 1024) dst_len = 1024;

    dst = (unsigned char *)malloc(dst_len);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    while (1) {
        mz_ulong try_len = dst_len;
        status = mz_uncompress(dst, &try_len, (const unsigned char *)src, (mz_ulong)src_len);
        if (status == MZ_OK) {
            basl_status_t ret = push_bytes(vm, dst, try_len, error);
            free(dst);
            return ret;
        }
        if (status == MZ_BUF_ERROR) {
            /* Need more space */
            mz_ulong new_len = dst_len * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_len);
            if (!new_dst) {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_len = new_len;
            continue;
        }
        /* Other error */
        free(dst);
        return push_empty_bytes(vm, error);
    }
}

/* ── Deflate (raw) ───────────────────────────────────────────────── */

static basl_status_t deflate_compress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong dst_len;
    unsigned char *dst;
    int status;
    basl_status_t ret;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    dst_len = mz_compressBound((mz_ulong)src_len);
    dst = (unsigned char *)malloc(dst_len);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    status = mz_compress(dst, &dst_len, (const unsigned char *)src, (mz_ulong)src_len);
    if (status != MZ_OK) {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    /* Strip zlib header (2 bytes) and trailer (4 bytes) to get raw deflate */
    if (dst_len > 6) {
        ret = push_bytes(vm, dst + 2, dst_len - 6, error);
        free(dst);
        return ret;
    }

    free(dst);
    return push_empty_bytes(vm, error);
}

static basl_status_t deflate_decompress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    tinfl_decompressor decomp;
    size_t dst_cap = 1024 * 64;
    size_t dst_len = 0;
    unsigned char *dst;
    tinfl_status status;
    size_t in_pos = 0;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    dst = (unsigned char *)malloc(dst_cap);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    tinfl_init(&decomp);

    while (in_pos < src_len) {
        size_t in_bytes = src_len - in_pos;
        size_t out_bytes = dst_cap - dst_len;
        int flags = (in_pos + in_bytes < src_len) ? TINFL_FLAG_HAS_MORE_INPUT : 0;

        status = tinfl_decompress(&decomp,
            (const mz_uint8 *)src + in_pos, &in_bytes,
            dst, dst + dst_len, &out_bytes, flags);

        in_pos += in_bytes;
        dst_len += out_bytes;

        if (status == TINFL_STATUS_DONE) {
            break;
        }
        if (status < 0) {
            free(dst);
            return push_empty_bytes(vm, error);
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT || dst_len >= dst_cap) {
            size_t new_cap = dst_cap * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_cap);
            if (!new_dst) {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }

    {
        basl_status_t ret = push_bytes(vm, dst, dst_len, error);
        free(dst);
        return ret;
    }
}

/* ── Gzip ────────────────────────────────────────────────────────── */

static basl_status_t gzip_compress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_ulong bound;
    unsigned char *out;
    size_t out_len;
    mz_stream stream;
    mz_uint32 crc;
    basl_status_t ret;

    /* Gzip header */
    static const unsigned char gzip_hdr[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
    };

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        /* Minimal valid gzip for empty */
        static const unsigned char empty_gz[] = {
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        return push_bytes(vm, empty_gz, sizeof(empty_gz), error);
    }

    bound = mz_deflateBound(NULL, (mz_ulong)src_len);
    out = (unsigned char *)malloc(10 + bound + 8);
    if (!out) {
        return push_empty_bytes(vm, error);
    }

    memcpy(out, gzip_hdr, 10);

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (const unsigned char *)src;
    stream.avail_in = (mz_uint32)src_len;
    stream.next_out = out + 10;
    stream.avail_out = (mz_uint32)bound;

    if (mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
        free(out);
        return push_empty_bytes(vm, error);
    }

    if (mz_deflate(&stream, MZ_FINISH) != MZ_STREAM_END) {
        mz_deflateEnd(&stream);
        free(out);
        return push_empty_bytes(vm, error);
    }

    out_len = 10 + stream.total_out;
    mz_deflateEnd(&stream);

    /* Append CRC32 and original size */
    crc = (mz_uint32)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)src, src_len);
    out[out_len++] = (unsigned char)(crc & 0xff);
    out[out_len++] = (unsigned char)((crc >> 8) & 0xff);
    out[out_len++] = (unsigned char)((crc >> 16) & 0xff);
    out[out_len++] = (unsigned char)((crc >> 24) & 0xff);
    out[out_len++] = (unsigned char)(src_len & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 8) & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 16) & 0xff);
    out[out_len++] = (unsigned char)((src_len >> 24) & 0xff);

    ret = push_bytes(vm, out, out_len, error);
    free(out);
    return ret;
}

static basl_status_t gzip_decompress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    size_t hdr_len = 10;
    const unsigned char *usrc;
    size_t deflate_len;
    tinfl_decompressor decomp;
    size_t dst_cap = 1024 * 64;
    size_t dst_len = 0;
    unsigned char *dst;
    size_t in_pos = 0;
    tinfl_status status;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len < 18) {
        return push_empty_bytes(vm, error);
    }

    usrc = (const unsigned char *)src;

    /* Verify gzip magic */
    if (usrc[0] != 0x1f || usrc[1] != 0x8b) {
        return push_empty_bytes(vm, error);
    }

    /* Skip optional fields */
    {
        unsigned char flags = usrc[3];
        if (flags & 0x04) {
            if (hdr_len + 2 > src_len) return push_empty_bytes(vm, error);
            hdr_len += 2 + (usrc[hdr_len] | (usrc[hdr_len + 1] << 8));
        }
        if (flags & 0x08) {
            while (hdr_len < src_len && usrc[hdr_len]) hdr_len++;
            hdr_len++;
        }
        if (flags & 0x10) {
            while (hdr_len < src_len && usrc[hdr_len]) hdr_len++;
            hdr_len++;
        }
        if (flags & 0x02) hdr_len += 2;
    }

    if (hdr_len + 8 > src_len) {
        return push_empty_bytes(vm, error);
    }

    deflate_len = src_len - hdr_len - 8;

    dst = (unsigned char *)malloc(dst_cap);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    tinfl_init(&decomp);

    while (in_pos < deflate_len) {
        size_t in_bytes = deflate_len - in_pos;
        size_t out_bytes = dst_cap - dst_len;
        int flags = (in_pos + in_bytes < deflate_len) ? TINFL_FLAG_HAS_MORE_INPUT : 0;

        status = tinfl_decompress(&decomp,
            usrc + hdr_len + in_pos, &in_bytes,
            dst, dst + dst_len, &out_bytes, flags);

        in_pos += in_bytes;
        dst_len += out_bytes;

        if (status == TINFL_STATUS_DONE) break;
        if (status < 0) {
            free(dst);
            return push_empty_bytes(vm, error);
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT || dst_len >= dst_cap) {
            size_t new_cap = dst_cap * 2;
            unsigned char *new_dst = (unsigned char *)realloc(dst, new_cap);
            if (!new_dst) {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }

    {
        basl_status_t ret = push_bytes(vm, dst, dst_len, error);
        free(dst);
        return ret;
    }
}

/* ── LZ4 ─────────────────────────────────────────────────────────── */

static basl_status_t lz4_compress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    int dst_cap, compressed_size;
    char *dst;
    basl_status_t ret;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    dst_cap = LZ4_compressBound((int)src_len);
    dst = (char *)malloc((size_t)dst_cap);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    compressed_size = LZ4_compress_default(src, dst, (int)src_len, dst_cap);
    if (compressed_size <= 0) {
        free(dst);
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, dst, (size_t)compressed_size, error);
    free(dst);
    return ret;
}

static basl_status_t lz4_decompress_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    int dst_cap, decompressed_size;
    char *dst;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!src || src_len == 0) {
        return push_empty_bytes(vm, error);
    }

    /* Start with 4x source size, grow if needed */
    dst_cap = (int)(src_len * 4);
    if (dst_cap < 1024) dst_cap = 1024;

    dst = (char *)malloc((size_t)dst_cap);
    if (!dst) {
        return push_empty_bytes(vm, error);
    }

    while (1) {
        decompressed_size = LZ4_decompress_safe(src, dst, (int)src_len, dst_cap);
        if (decompressed_size > 0) {
            basl_status_t ret = push_bytes(vm, dst, (size_t)decompressed_size, error);
            free(dst);
            return ret;
        }
        if (decompressed_size == 0) {
            /* Empty result */
            free(dst);
            return push_empty_bytes(vm, error);
        }
        /* Negative = error, try larger buffer */
        if (dst_cap > 256 * 1024 * 1024) {
            /* Give up after 256MB */
            free(dst);
            return push_empty_bytes(vm, error);
        }
        {
            int new_cap = dst_cap * 2;
            char *new_dst = (char *)realloc(dst, (size_t)new_cap);
            if (!new_dst) {
                free(dst);
                return push_empty_bytes(vm, error);
            }
            dst = new_dst;
            dst_cap = new_cap;
        }
    }
}

/* ── ZIP ─────────────────────────────────────────────────────────── */

static basl_status_t zip_list_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    mz_zip_archive zip;
    mz_uint i, num_files;
    basl_object_t *arr = NULL;
    basl_status_t s;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != BASL_STATUS_OK) return s;

    if (!src || src_len == 0) {
        goto done;
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, src, src_len, 0)) {
        goto done;
    }

    num_files = mz_zip_reader_get_num_files(&zip);
    for (i = 0; i < num_files; i++) {
        char filename[512];
        mz_zip_reader_get_filename(&zip, i, filename, sizeof(filename));
        
        basl_object_t *str_obj = NULL;
        s = basl_string_object_new(basl_vm_runtime(vm), filename, strlen(filename), &str_obj, error);
        if (s == BASL_STATUS_OK) {
            basl_value_t val;
            basl_value_init_object(&val, &str_obj);
            basl_array_object_append(arr, &val, error);
            basl_value_release(&val);
        }
    }
    mz_zip_reader_end(&zip);

done:
    {
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }
}

static basl_status_t zip_read_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t zip_len, name_len;
    const char *zip_data, *name;
    mz_zip_archive zip;
    int file_index;
    void *data;
    size_t data_size;
    basl_status_t ret;

    zip_data = get_bytes_data(basl_vm_stack_get(vm, base), &zip_len);
    name = get_bytes_data(basl_vm_stack_get(vm, base + 1), &name_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!zip_data || zip_len == 0 || !name || name_len == 0) {
        return push_empty_bytes(vm, error);
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_mem(&zip, zip_data, zip_len, 0)) {
        return push_empty_bytes(vm, error);
    }

    file_index = mz_zip_reader_locate_file(&zip, name, NULL, 0);
    if (file_index < 0) {
        mz_zip_reader_end(&zip);
        return push_empty_bytes(vm, error);
    }

    data = mz_zip_reader_extract_to_heap(&zip, (mz_uint)file_index, &data_size, 0);
    mz_zip_reader_end(&zip);

    if (!data) {
        return push_empty_bytes(vm, error);
    }

    ret = push_bytes(vm, data, data_size, error);
    mz_free(data);
    return ret;
}

/* zip_create: takes two arrays - names and contents */
static basl_status_t zip_create_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_value_t names_val = basl_vm_stack_get(vm, base);
    basl_value_t contents_val = basl_vm_stack_get(vm, base + 1);
    const basl_object_t *names_obj, *contents_obj;
    mz_zip_archive zip;
    void *zip_data = NULL;
    size_t zip_size = 0;
    size_t i, count;
    basl_status_t ret;

    basl_vm_stack_pop_n(vm, arg_count);

    if (!basl_nanbox_is_object(names_val) || !basl_nanbox_is_object(contents_val)) {
        return push_empty_bytes(vm, error);
    }
    names_obj = (const basl_object_t *)basl_nanbox_decode_ptr(names_val);
    contents_obj = (const basl_object_t *)basl_nanbox_decode_ptr(contents_val);
    if (!names_obj || basl_object_type(names_obj) != BASL_OBJECT_ARRAY ||
        !contents_obj || basl_object_type(contents_obj) != BASL_OBJECT_ARRAY) {
        return push_empty_bytes(vm, error);
    }

    count = basl_array_object_length(names_obj);
    if (basl_array_object_length(contents_obj) < count) {
        count = basl_array_object_length(contents_obj);
    }

    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        return push_empty_bytes(vm, error);
    }

    for (i = 0; i < count; i++) {
        basl_value_t name_val, content_val;
        size_t name_len, data_len;
        const char *name, *data;

        if (!basl_array_object_get(names_obj, i, &name_val)) continue;
        if (!basl_array_object_get(contents_obj, i, &content_val)) continue;

        name = get_bytes_data(name_val, &name_len);
        data = get_bytes_data(content_val, &data_len);
        if (!name) continue;

        mz_zip_writer_add_mem(&zip, name, data ? data : "", (mz_uint)data_len, MZ_DEFAULT_LEVEL);
    }

    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size)) {
        mz_zip_writer_end(&zip);
        return push_empty_bytes(vm, error);
    }
    mz_zip_writer_end(&zip);

    ret = push_bytes(vm, zip_data, zip_size, error);
    mz_free(zip_data);
    return ret;
}

/* ── TAR ─────────────────────────────────────────────────────────── */

/* POSIX ustar header (512 bytes) */
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} tar_header_t;

static unsigned int tar_checksum(const tar_header_t *h) {
    const unsigned char *p = (const unsigned char *)h;
    unsigned int sum = 0;
    size_t i;
    for (i = 0; i < 512; i++) {
        if (i >= 148 && i < 156) sum += ' ';  /* checksum field treated as spaces */
        else sum += p[i];
    }
    return sum;
}

static size_t tar_parse_octal(const char *s, size_t len) {
    size_t val = 0;
    size_t i;
    for (i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++) {
        val = val * 8 + (size_t)(s[i] - '0');
    }
    return val;
}

static void tar_write_octal(char *dst, size_t len, size_t val) {
    size_t i = len - 1;
    dst[i--] = '\0';
    while (i > 0) {
        dst[i--] = '0' + (char)(val & 7);
        val >>= 3;
    }
    dst[0] = '0';
}

static basl_status_t tar_list_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t src_len;
    const char *src;
    basl_object_t *arr = NULL;
    basl_status_t s;
    size_t pos = 0;

    src = get_bytes_data(basl_vm_stack_get(vm, base), &src_len);
    basl_vm_stack_pop_n(vm, arg_count);

    s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != BASL_STATUS_OK) return s;

    while (pos + 512 <= src_len) {
        const tar_header_t *h = (const tar_header_t *)(src + pos);
        size_t file_size;
        char name[256];
        size_t name_len;

        if (h->name[0] == '\0') break;  /* End of archive */

        /* Build full name from prefix + name */
        if (h->prefix[0] && memcmp(h->magic, "ustar", 5) == 0) {
            size_t plen = strnlen(h->prefix, 155);
            size_t nlen = strnlen(h->name, 100);
            memcpy(name, h->prefix, plen);
            name[plen] = '/';
            memcpy(name + plen + 1, h->name, nlen);
            name_len = plen + 1 + nlen;
        } else {
            name_len = strnlen(h->name, 100);
            memcpy(name, h->name, name_len);
        }
        name[name_len] = '\0';

        file_size = tar_parse_octal(h->size, 12);

        /* Add to array */
        {
            basl_object_t *str_obj = NULL;
            s = basl_string_object_new(basl_vm_runtime(vm), name, name_len, &str_obj, error);
            if (s == BASL_STATUS_OK) {
                basl_value_t val;
                basl_value_init_object(&val, &str_obj);
                basl_array_object_append(arr, &val, error);
                basl_value_release(&val);
            }
        }

        pos += 512 + ((file_size + 511) & ~511);
    }

    {
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }
}

static basl_status_t tar_read_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t tar_len, name_len;
    const char *tar_data, *name;
    size_t pos = 0;

    tar_data = get_bytes_data(basl_vm_stack_get(vm, base), &tar_len);
    name = get_bytes_data(basl_vm_stack_get(vm, base + 1), &name_len);
    basl_vm_stack_pop_n(vm, arg_count);

    if (!tar_data || tar_len == 0 || !name || name_len == 0) {
        return push_empty_bytes(vm, error);
    }

    while (pos + 512 <= tar_len) {
        const tar_header_t *h = (const tar_header_t *)(tar_data + pos);
        size_t file_size;
        char entry_name[256];
        size_t entry_len;

        if (h->name[0] == '\0') break;

        if (h->prefix[0] && memcmp(h->magic, "ustar", 5) == 0) {
            size_t plen = strnlen(h->prefix, 155);
            size_t nlen = strnlen(h->name, 100);
            memcpy(entry_name, h->prefix, plen);
            entry_name[plen] = '/';
            memcpy(entry_name + plen + 1, h->name, nlen);
            entry_len = plen + 1 + nlen;
        } else {
            entry_len = strnlen(h->name, 100);
            memcpy(entry_name, h->name, entry_len);
        }
        entry_name[entry_len] = '\0';

        file_size = tar_parse_octal(h->size, 12);

        if (entry_len == name_len && memcmp(entry_name, name, name_len) == 0) {
            if (pos + 512 + file_size <= tar_len) {
                return push_bytes(vm, tar_data + pos + 512, file_size, error);
            }
            break;
        }

        pos += 512 + ((file_size + 511) & ~511);
    }

    return push_empty_bytes(vm, error);
}

static basl_status_t tar_create_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_value_t names_val = basl_vm_stack_get(vm, base);
    basl_value_t contents_val = basl_vm_stack_get(vm, base + 1);
    const basl_object_t *names_obj, *contents_obj;
    size_t i, count;
    unsigned char *tar_data = NULL;
    size_t tar_size = 0, tar_cap = 0;
    basl_status_t ret;

    basl_vm_stack_pop_n(vm, arg_count);

    if (!basl_nanbox_is_object(names_val) || !basl_nanbox_is_object(contents_val)) {
        return push_empty_bytes(vm, error);
    }
    names_obj = (const basl_object_t *)basl_nanbox_decode_ptr(names_val);
    contents_obj = (const basl_object_t *)basl_nanbox_decode_ptr(contents_val);
    if (!names_obj || basl_object_type(names_obj) != BASL_OBJECT_ARRAY ||
        !contents_obj || basl_object_type(contents_obj) != BASL_OBJECT_ARRAY) {
        return push_empty_bytes(vm, error);
    }

    count = basl_array_object_length(names_obj);
    if (basl_array_object_length(contents_obj) < count) {
        count = basl_array_object_length(contents_obj);
    }

    for (i = 0; i < count; i++) {
        basl_value_t name_val, content_val;
        size_t name_len, data_len;
        const char *name, *data;
        tar_header_t h;
        size_t padded_size, needed;
        unsigned int cksum;

        if (!basl_array_object_get(names_obj, i, &name_val)) continue;
        if (!basl_array_object_get(contents_obj, i, &content_val)) continue;

        name = get_bytes_data(name_val, &name_len);
        data = get_bytes_data(content_val, &data_len);
        if (!name || name_len == 0) continue;
        if (name_len > 100) name_len = 100;

        memset(&h, 0, sizeof(h));
        memcpy(h.name, name, name_len);
        memcpy(h.mode, "0000644", 7);
        memcpy(h.uid, "0000000", 7);
        memcpy(h.gid, "0000000", 7);
        tar_write_octal(h.size, 12, data_len);
        tar_write_octal(h.mtime, 12, 0);
        h.typeflag = '0';
        memcpy(h.magic, "ustar", 5);
        h.version[0] = '0';
        h.version[1] = '0';

        memset(h.checksum, ' ', 8);
        cksum = tar_checksum(&h);
        snprintf(h.checksum, 8, "%06o", cksum);
        h.checksum[6] = '\0';
        h.checksum[7] = ' ';

        padded_size = (data_len + 511) & ~511;
        needed = tar_size + 512 + padded_size;
        if (needed > tar_cap) {
            size_t new_cap = tar_cap ? tar_cap * 2 : 4096;
            while (new_cap < needed) new_cap *= 2;
            unsigned char *new_data = (unsigned char *)realloc(tar_data, new_cap);
            if (!new_data) {
                free(tar_data);
                return push_empty_bytes(vm, error);
            }
            tar_data = new_data;
            tar_cap = new_cap;
        }

        memcpy(tar_data + tar_size, &h, 512);
        tar_size += 512;
        if (data && data_len > 0) {
            memcpy(tar_data + tar_size, data, data_len);
        }
        if (padded_size > data_len) {
            memset(tar_data + tar_size + data_len, 0, padded_size - data_len);
        }
        tar_size += padded_size;
    }

    /* Add two empty blocks at end */
    {
        size_t needed = tar_size + 1024;
        if (needed > tar_cap) {
            unsigned char *new_data = (unsigned char *)realloc(tar_data, needed);
            if (!new_data) {
                free(tar_data);
                return push_empty_bytes(vm, error);
            }
            tar_data = new_data;
        }
        memset(tar_data + tar_size, 0, 1024);
        tar_size += 1024;
    }

    ret = push_bytes(vm, tar_data, tar_size, error);
    free(tar_data);
    return ret;
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int bytes_param[] = { BASL_TYPE_STRING };
static const int two_bytes_param[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int two_arrays_param[] = { BASL_TYPE_OBJECT, BASL_TYPE_OBJECT };

/* Extended type info for functions that take array<string> parameters */
static const basl_native_type_t create_params_ext[] = {
    BASL_NATIVE_TYPE_ARRAY(BASL_TYPE_STRING),
    BASL_NATIVE_TYPE_ARRAY(BASL_TYPE_STRING)
};

/* Extended type info for functions that return array<string> */
static const basl_native_type_t array_string_return = BASL_NATIVE_TYPE_ARRAY(BASL_TYPE_STRING);

static const basl_native_module_function_t compress_functions[] = {
    {"deflate_compress", 16U, deflate_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"deflate_decompress", 18U, deflate_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zlib_compress", 13U, zlib_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zlib_decompress", 15U, zlib_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"gzip_compress", 13U, gzip_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"gzip_decompress", 15U, gzip_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"lz4_compress", 12U, lz4_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"lz4_decompress", 14U, lz4_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zip_list", 8U, zip_list_fn, 1U, bytes_param, BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING, NULL, &array_string_return},
    {"zip_read", 8U, zip_read_fn, 2U, two_bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"zip_create", 10U, zip_create_fn, 2U, two_arrays_param, BASL_TYPE_STRING, 1U, NULL, 0, create_params_ext, NULL},
    {"tar_list", 8U, tar_list_fn, 1U, bytes_param, BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING, NULL, &array_string_return},
    {"tar_read", 8U, tar_read_fn, 2U, two_bytes_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"tar_create", 10U, tar_create_fn, 2U, two_arrays_param, BASL_TYPE_STRING, 1U, NULL, 0, create_params_ext, NULL},
};

#define COMPRESS_FUNCTION_COUNT (sizeof(compress_functions) / sizeof(compress_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_compress = {
    "compress", 8U,
    compress_functions,
    COMPRESS_FUNCTION_COUNT,
    NULL, 0U
};
