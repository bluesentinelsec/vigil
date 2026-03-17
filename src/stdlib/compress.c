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

/* ── Module descriptor ───────────────────────────────────────────── */

static const int bytes_param[] = { BASL_TYPE_STRING };

static const basl_native_module_function_t compress_functions[] = {
    {"deflate_compress", 16U, deflate_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"deflate_decompress", 18U, deflate_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"zlib_compress", 13U, zlib_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"zlib_decompress", 15U, zlib_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"gzip_compress", 13U, gzip_compress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"gzip_decompress", 15U, gzip_decompress_fn, 1U, bytes_param, BASL_TYPE_STRING, 1U, NULL, 0},
};

#define COMPRESS_FUNCTION_COUNT (sizeof(compress_functions) / sizeof(compress_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_compress = {
    "compress", 8U,
    compress_functions,
    COMPRESS_FUNCTION_COUNT,
    NULL, 0U
};
