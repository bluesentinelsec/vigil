#ifndef BASL_PKG_H
#define BASL_PKG_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Package specifier ───────────────────────────────────────────── */

typedef struct basl_pkg_spec {
    char *url;      /* e.g. "github.com/user/repo" */
    char *version;  /* e.g. "v1.0.0" or NULL for latest */
} basl_pkg_spec_t;

BASL_API void basl_pkg_spec_free(basl_pkg_spec_t *spec);

BASL_API basl_status_t basl_pkg_parse_spec(
    const char *input,
    basl_pkg_spec_t *out_spec,
    basl_error_t *error
);

/* ── Lock file ───────────────────────────────────────────────────── */

typedef struct basl_pkg_lock_entry {
    char *name;
    char *version;
    char *commit;
} basl_pkg_lock_entry_t;

typedef struct basl_pkg_lock {
    basl_pkg_lock_entry_t *entries;
    size_t count;
    size_t capacity;
} basl_pkg_lock_t;

BASL_API void basl_pkg_lock_init(basl_pkg_lock_t *lock);
BASL_API void basl_pkg_lock_free(basl_pkg_lock_t *lock);

BASL_API basl_status_t basl_pkg_lock_add(
    basl_pkg_lock_t *lock,
    const char *name,
    const char *version,
    const char *commit,
    basl_error_t *error
);

BASL_API const basl_pkg_lock_entry_t *basl_pkg_lock_find(
    const basl_pkg_lock_t *lock,
    const char *name
);

BASL_API basl_status_t basl_pkg_lock_read(
    const char *path,
    basl_pkg_lock_t *out_lock,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_lock_write(
    const char *path,
    const basl_pkg_lock_t *lock,
    basl_error_t *error
);

/* ── Git operations ──────────────────────────────────────────────── */

BASL_API basl_status_t basl_pkg_git_available(basl_error_t *error);

BASL_API basl_status_t basl_pkg_git_clone(
    const char *url,
    const char *dest,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_git_fetch(
    const char *repo_path,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_git_checkout(
    const char *repo_path,
    const char *version,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_git_head(
    const char *repo_path,
    char *out_commit,
    size_t commit_size,
    basl_error_t *error
);

/* ── High-level operations ───────────────────────────────────────── */

BASL_API basl_status_t basl_pkg_get(
    const char *project_root,
    const char *specifier,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_sync(
    const char *project_root,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_remove(
    const char *project_root,
    const char *package_url,
    basl_error_t *error
);

BASL_API basl_status_t basl_pkg_resolve_import(
    const char *project_root,
    const char *import_path,
    char *out_path,
    size_t path_size,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
