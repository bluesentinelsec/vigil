/*
 * BASL Package Manager
 *
 * Provides Go-style package management using git for distribution.
 * Packages are identified by git URLs (e.g. github.com/user/repo).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/pkg.h"
#include "basl/toml.h"
#include "platform/platform.h"

#ifdef _MSC_VER
#define pkg_strdup _strdup
#else
#define pkg_strdup strdup
#endif

/* ── Helpers ─────────────────────────────────────────────────────── */

static void set_error(basl_error_t *error, basl_status_t type, const char *msg) {
    if (error == NULL) return;
    basl_error_clear(error);
    error->type = type;
    error->value = msg;
    error->length = msg ? strlen(msg) : 0;
}

static char *trim_whitespace(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == '\0') return str;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    end[1] = '\0';
    return str;
}

/* ── Package specifier ───────────────────────────────────────────── */

void basl_pkg_spec_free(basl_pkg_spec_t *spec) {
    if (spec == NULL) return;
    free(spec->url);
    free(spec->version);
    spec->url = NULL;
    spec->version = NULL;
}

basl_status_t basl_pkg_parse_spec(
    const char *input,
    basl_pkg_spec_t *out_spec,
    basl_error_t *error
) {
    const char *at;
    size_t url_len;

    if (input == NULL || out_spec == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    memset(out_spec, 0, sizeof(*out_spec));
    at = strchr(input, '@');

    if (at != NULL) {
        url_len = (size_t)(at - input);
        out_spec->url = malloc(url_len + 1);
        if (out_spec->url == NULL) {
            set_error(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(out_spec->url, input, url_len);
        out_spec->url[url_len] = '\0';

        out_spec->version = pkg_strdup(at + 1);
        if (out_spec->version == NULL) {
            free(out_spec->url);
            out_spec->url = NULL;
            set_error(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
            return BASL_STATUS_OUT_OF_MEMORY;
        }
    } else {
        out_spec->url = pkg_strdup(input);
        if (out_spec->url == NULL) {
            set_error(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        out_spec->version = NULL;
    }

    return BASL_STATUS_OK;
}

/* ── Lock file ───────────────────────────────────────────────────── */

void basl_pkg_lock_init(basl_pkg_lock_t *lock) {
    if (lock == NULL) return;
    lock->entries = NULL;
    lock->count = 0;
    lock->capacity = 0;
}

void basl_pkg_lock_free(basl_pkg_lock_t *lock) {
    size_t i;
    if (lock == NULL) return;
    for (i = 0; i < lock->count; i++) {
        free(lock->entries[i].name);
        free(lock->entries[i].version);
        free(lock->entries[i].commit);
    }
    free(lock->entries);
    lock->entries = NULL;
    lock->count = 0;
    lock->capacity = 0;
}

basl_status_t basl_pkg_lock_add(
    basl_pkg_lock_t *lock,
    const char *name,
    const char *version,
    const char *commit,
    basl_error_t *error
) {
    basl_pkg_lock_entry_t *entry;
    size_t i;

    if (lock == NULL || name == NULL || commit == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Update existing entry if present */
    for (i = 0; i < lock->count; i++) {
        if (strcmp(lock->entries[i].name, name) == 0) {
            free(lock->entries[i].version);
            free(lock->entries[i].commit);
            lock->entries[i].version = version ? pkg_strdup(version) : NULL;
            lock->entries[i].commit = pkg_strdup(commit);
            return BASL_STATUS_OK;
        }
    }

    /* Grow capacity if needed */
    if (lock->count >= lock->capacity) {
        size_t new_cap = lock->capacity == 0 ? 8 : lock->capacity * 2;
        basl_pkg_lock_entry_t *new_entries = realloc(
            lock->entries, new_cap * sizeof(basl_pkg_lock_entry_t));
        if (new_entries == NULL) {
            set_error(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        lock->entries = new_entries;
        lock->capacity = new_cap;
    }

    entry = &lock->entries[lock->count];
    entry->name = pkg_strdup(name);
    entry->version = version ? pkg_strdup(version) : NULL;
    entry->commit = pkg_strdup(commit);

    if (entry->name == NULL || entry->commit == NULL) {
        free(entry->name);
        free(entry->version);
        free(entry->commit);
        set_error(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    lock->count++;
    return BASL_STATUS_OK;
}

const basl_pkg_lock_entry_t *basl_pkg_lock_find(
    const basl_pkg_lock_t *lock,
    const char *name
) {
    size_t i;
    if (lock == NULL || name == NULL) return NULL;
    for (i = 0; i < lock->count; i++) {
        if (strcmp(lock->entries[i].name, name) == 0) {
            return &lock->entries[i];
        }
    }
    return NULL;
}

/* ── Lock file I/O ───────────────────────────────────────────────── */

basl_status_t basl_pkg_lock_read(
    const char *path,
    basl_pkg_lock_t *out_lock,
    basl_error_t *error
) {
    char *data = NULL;
    size_t length;
    basl_toml_value_t *root = NULL;
    const basl_toml_value_t *packages;
    size_t i, count;
    basl_status_t status;

    if (path == NULL || out_lock == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_pkg_lock_init(out_lock);

    status = basl_platform_read_file(NULL, path, &data, &length, error);
    if (status != BASL_STATUS_OK) return status;

    status = basl_toml_parse(NULL, data, length, &root, error);
    free(data);
    if (status != BASL_STATUS_OK) return status;

    packages = basl_toml_table_get(root, "package");
    if (packages == NULL || basl_toml_type(packages) != BASL_TOML_ARRAY) {
        basl_toml_free(&root);
        return BASL_STATUS_OK; /* Empty lock file */
    }

    count = basl_toml_array_count(packages);
    for (i = 0; i < count; i++) {
        const basl_toml_value_t *pkg = basl_toml_array_get(packages, i);
        const basl_toml_value_t *name_val, *version_val, *commit_val;
        const char *name, *version, *commit;

        if (pkg == NULL || basl_toml_type(pkg) != BASL_TOML_TABLE) continue;

        name_val = basl_toml_table_get(pkg, "name");
        version_val = basl_toml_table_get(pkg, "version");
        commit_val = basl_toml_table_get(pkg, "commit");

        if (name_val == NULL || basl_toml_type(name_val) != BASL_TOML_STRING) continue;
        if (commit_val == NULL || basl_toml_type(commit_val) != BASL_TOML_STRING) continue;

        name = basl_toml_string_value(name_val);
        commit = basl_toml_string_value(commit_val);
        version = (version_val && basl_toml_type(version_val) == BASL_TOML_STRING)
            ? basl_toml_string_value(version_val) : NULL;

        status = basl_pkg_lock_add(out_lock, name, version, commit, error);
        if (status != BASL_STATUS_OK) {
            basl_pkg_lock_free(out_lock);
            basl_toml_free(&root);
            return status;
        }
    }

    basl_toml_free(&root);
    return BASL_STATUS_OK;
}

basl_status_t basl_pkg_lock_write(
    const char *path,
    const basl_pkg_lock_t *lock,
    basl_error_t *error
) {
    FILE *f;
    size_t i;

    if (path == NULL || lock == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    f = fopen(path, "w");
    if (f == NULL) {
        set_error(error, BASL_STATUS_INTERNAL, "failed to open lock file for writing");
        return BASL_STATUS_INTERNAL;
    }

    fprintf(f, "# AUTO-GENERATED by basl get - DO NOT EDIT\n\n");

    for (i = 0; i < lock->count; i++) {
        const basl_pkg_lock_entry_t *e = &lock->entries[i];
        fprintf(f, "[[package]]\n");
        fprintf(f, "name = \"%s\"\n", e->name);
        if (e->version != NULL) {
            fprintf(f, "version = \"%s\"\n", e->version);
        }
        fprintf(f, "commit = \"%s\"\n\n", e->commit);
    }

    fclose(f);
    return BASL_STATUS_OK;
}

/* ── Git operations ──────────────────────────────────────────────── */

basl_status_t basl_pkg_git_available(basl_error_t *error) {
    const char *argv[] = {"git", "--version", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    basl_status_t status;

    status = basl_platform_exec(argv, &out, &err_out, &exit_code, error);
    free(out);
    free(err_out);

    if (status != BASL_STATUS_OK || exit_code != 0) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT,
            "git is not installed. Install git and try again.");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_pkg_git_clone(
    const char *url,
    const char *dest,
    basl_error_t *error
) {
    char git_url[1024];
    const char *argv[] = {"git", "clone", git_url, dest, NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    basl_status_t status;

    if (url == NULL || dest == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Convert package URL to git SSH URL (e.g. github.com/user/repo -> git@github.com:user/repo.git) */
    {
        const char *slash = strchr(url, '/');
        if (slash != NULL) {
            size_t host_len = (size_t)(slash - url);
            snprintf(git_url, sizeof(git_url), "git@%.*s:%s.git", (int)host_len, url, slash + 1);
        } else {
            snprintf(git_url, sizeof(git_url), "git@%s.git", url);
        }
    }

    status = basl_platform_exec(argv, &out, &err_out, &exit_code, error);

    if (status != BASL_STATUS_OK) {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0) {
        set_error(error, BASL_STATUS_INTERNAL, err_out ? err_out : "git clone failed");
        free(out);
        free(err_out);
        return BASL_STATUS_INTERNAL;
    }

    free(out);
    free(err_out);
    return BASL_STATUS_OK;
}

basl_status_t basl_pkg_git_fetch(
    const char *repo_path,
    basl_error_t *error
) {
    const char *argv[] = {"git", "-C", repo_path, "fetch", "--tags", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    basl_status_t status;

    if (repo_path == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_platform_exec(argv, &out, &err_out, &exit_code, error);
    free(out);
    free(err_out);

    if (status != BASL_STATUS_OK) return status;
    if (exit_code != 0) {
        set_error(error, BASL_STATUS_INTERNAL, "git fetch failed");
        return BASL_STATUS_INTERNAL;
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_pkg_git_checkout(
    const char *repo_path,
    const char *version,
    basl_error_t *error
) {
    const char *argv[] = {"git", "-C", repo_path, "checkout", version, NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    basl_status_t status;

    if (repo_path == NULL || version == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_platform_exec(argv, &out, &err_out, &exit_code, error);

    if (status != BASL_STATUS_OK) {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, err_out ? err_out : "version not found");
        free(out);
        free(err_out);
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    free(out);
    free(err_out);
    return BASL_STATUS_OK;
}

basl_status_t basl_pkg_git_head(
    const char *repo_path,
    char *out_commit,
    size_t commit_size,
    basl_error_t *error
) {
    const char *argv[] = {"git", "-C", repo_path, "rev-parse", "HEAD", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    basl_status_t status;
    char *trimmed;

    if (repo_path == NULL || out_commit == NULL || commit_size == 0) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_platform_exec(argv, &out, &err_out, &exit_code, error);

    if (status != BASL_STATUS_OK) {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0 || out == NULL) {
        set_error(error, BASL_STATUS_INTERNAL, "failed to get HEAD commit");
        free(out);
        free(err_out);
        return BASL_STATUS_INTERNAL;
    }

    trimmed = trim_whitespace(out);
    if (strlen(trimmed) >= commit_size) {
        free(out);
        free(err_out);
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "commit buffer too small");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    strcpy(out_commit, trimmed);
    free(out);
    free(err_out);
    return BASL_STATUS_OK;
}

/* ── TOML helpers ────────────────────────────────────────────────── */

static basl_status_t read_project_toml(
    const char *project_root,
    basl_toml_value_t **out_root,
    basl_error_t *error
) {
    char toml_path[4096];
    char *data = NULL;
    size_t length;
    basl_status_t status;

    if (basl_platform_path_join(project_root, "basl.toml", toml_path,
            sizeof(toml_path), error) != BASL_STATUS_OK) {
        return error->type;
    }

    status = basl_platform_read_file(NULL, toml_path, &data, &length, error);
    if (status != BASL_STATUS_OK) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "basl.toml not found");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_toml_parse(NULL, data, length, out_root, error);
    free(data);
    return status;
}

static basl_status_t write_project_toml(
    const char *project_root,
    basl_toml_value_t *root,
    basl_error_t *error
) {
    char toml_path[4096];
    char *output = NULL;
    size_t output_len;
    basl_status_t status;

    if (basl_platform_path_join(project_root, "basl.toml", toml_path,
            sizeof(toml_path), error) != BASL_STATUS_OK) {
        return error->type;
    }

    status = basl_toml_emit(root, &output, &output_len, error);
    if (status != BASL_STATUS_OK) return status;

    status = basl_platform_write_file(toml_path, output, output_len, error);
    free(output);
    return status;
}

static basl_status_t ensure_deps_table(
    basl_toml_value_t *root,
    basl_toml_value_t **out_deps,
    basl_error_t *error
) {
    const basl_toml_value_t *existing;
    basl_toml_value_t *deps = NULL;

    existing = basl_toml_table_get(root, "deps");
    if (existing != NULL) {
        *out_deps = (basl_toml_value_t *)existing;
        return BASL_STATUS_OK;
    }

    if (basl_toml_table_new(NULL, &deps, error) != BASL_STATUS_OK) {
        return error->type;
    }

    if (basl_toml_table_set(root, "deps", 4, deps, error) != BASL_STATUS_OK) {
        basl_toml_free(&deps);
        return error->type;
    }

    *out_deps = deps;
    return BASL_STATUS_OK;
}

/* Forward declaration for transitive deps */
static basl_status_t install_transitive_deps(
    const char *project_root,
    const char *pkg_path,
    basl_pkg_lock_t *lock,
    basl_error_t *error
);

/* Install a single package (used by both get and sync) */
static basl_status_t install_package(
    const char *project_root,
    const char *pkg_url,
    const char *version,
    basl_pkg_lock_t *lock,
    char *out_commit,
    size_t commit_size,
    basl_error_t *error
) {
    char deps_path[4096];
    char pkg_path[4096];
    int pkg_exists = 0;
    basl_status_t status;

    /* Build paths */
    if (basl_platform_path_join(project_root, "deps", deps_path,
            sizeof(deps_path), error) != BASL_STATUS_OK) {
        return error->type;
    }
    if (basl_platform_path_join(deps_path, pkg_url, pkg_path,
            sizeof(pkg_path), error) != BASL_STATUS_OK) {
        return error->type;
    }

    /* Ensure deps/ directory exists */
    basl_platform_mkdir_p(deps_path, error);

    /* Check if package already exists */
    basl_platform_file_exists(pkg_path, &pkg_exists);

    if (pkg_exists) {
        /* Fetch updates */
        status = basl_pkg_git_fetch(pkg_path, error);
        if (status != BASL_STATUS_OK) return status;
    } else {
        /* Create parent directories */
        char parent[4096];
        size_t len = strlen(pkg_path);
        memcpy(parent, pkg_path, len + 1);
        while (len > 0 && parent[len - 1] != '/' && parent[len - 1] != '\\') len--;
        if (len > 0) parent[len - 1] = '\0';
        basl_platform_mkdir_p(parent, error);

        status = basl_pkg_git_clone(pkg_url, pkg_path, error);
        if (status != BASL_STATUS_OK) return status;
    }

    /* Checkout specific version if requested */
    if (version != NULL && strlen(version) > 0) {
        status = basl_pkg_git_checkout(pkg_path, version, error);
        if (status != BASL_STATUS_OK) return status;
    }

    /* Get current commit */
    status = basl_pkg_git_head(pkg_path, out_commit, commit_size, error);
    if (status != BASL_STATUS_OK) return status;

    /* Update lock */
    basl_pkg_lock_add(lock, pkg_url, version, out_commit, error);

    /* Install transitive dependencies */
    install_transitive_deps(project_root, pkg_path, lock, error);

    return BASL_STATUS_OK;
}

/* Install transitive dependencies from a package's basl.toml */
static basl_status_t install_transitive_deps(
    const char *project_root,
    const char *pkg_path,
    basl_pkg_lock_t *lock,
    basl_error_t *error
) {
    char toml_path[4096];
    char *data = NULL;
    size_t length;
    basl_toml_value_t *root = NULL;
    const basl_toml_value_t *deps;
    size_t i, count;
    int exists = 0;

    /* Check if package has basl.toml */
    if (basl_platform_path_join(pkg_path, "basl.toml", toml_path,
            sizeof(toml_path), error) != BASL_STATUS_OK) {
        return BASL_STATUS_OK; /* Not fatal */
    }

    basl_platform_file_exists(toml_path, &exists);
    if (!exists) return BASL_STATUS_OK;

    if (basl_platform_read_file(NULL, toml_path, &data, &length, error) != BASL_STATUS_OK) {
        return BASL_STATUS_OK; /* Not fatal */
    }

    if (basl_toml_parse(NULL, data, length, &root, error) != BASL_STATUS_OK) {
        free(data);
        return BASL_STATUS_OK; /* Not fatal */
    }
    free(data);

    deps = basl_toml_table_get(root, "deps");
    if (deps == NULL || basl_toml_type(deps) != BASL_TOML_TABLE) {
        basl_toml_free(&root);
        return BASL_STATUS_OK;
    }

    count = basl_toml_table_count(deps);
    for (i = 0; i < count; i++) {
        const char *dep_url = NULL;
        size_t dep_url_len;
        const basl_toml_value_t *version_val;
        const char *version = NULL;
        char commit[64];
        char url_copy[1024];

        if (basl_toml_table_entry(deps, i, &dep_url, &dep_url_len, &version_val) != BASL_STATUS_OK)
            continue;

        /* Skip if already in lock (already installed) */
        snprintf(url_copy, sizeof(url_copy), "%.*s", (int)dep_url_len, dep_url);
        if (basl_pkg_lock_find(lock, url_copy) != NULL)
            continue;

        if (version_val != NULL && basl_toml_type(version_val) == BASL_TOML_STRING) {
            version = basl_toml_string_value(version_val);
        }

        /* Install this transitive dependency */
        install_package(project_root, url_copy, version, lock, commit, sizeof(commit), error);
    }

    basl_toml_free(&root);
    return BASL_STATUS_OK;
}

/* ── High-level: basl_pkg_get ────────────────────────────────────── */

basl_status_t basl_pkg_get(
    const char *project_root,
    const char *specifier,
    basl_error_t *error
) {
    basl_pkg_spec_t spec = {0};
    basl_pkg_lock_t lock = {0};
    basl_toml_value_t *root = NULL;
    basl_toml_value_t *deps = NULL;
    basl_toml_value_t *version_val = NULL;
    char lock_path[4096];
    char commit[64];
    basl_status_t status;
    const char *version_str;

    if (project_root == NULL || specifier == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Check git is available */
    status = basl_pkg_git_available(error);
    if (status != BASL_STATUS_OK) return status;

    /* Parse specifier */
    status = basl_pkg_parse_spec(specifier, &spec, error);
    if (status != BASL_STATUS_OK) return status;

    /* Build lock path */
    if (basl_platform_path_join(project_root, "basl.lock", lock_path,
            sizeof(lock_path), error) != BASL_STATUS_OK) {
        goto cleanup;
    }

    /* Read existing lock file (ignore errors - may not exist) */
    basl_pkg_lock_init(&lock);
    basl_pkg_lock_read(lock_path, &lock, NULL);

    /* Install package and transitive deps */
    status = install_package(project_root, spec.url, spec.version, &lock,
        commit, sizeof(commit), error);
    if (status != BASL_STATUS_OK) goto cleanup;

    /* Write lock file */
    status = basl_pkg_lock_write(lock_path, &lock, error);
    if (status != BASL_STATUS_OK) goto cleanup;

    /* Update basl.toml */
    status = read_project_toml(project_root, &root, error);
    if (status != BASL_STATUS_OK) goto cleanup;

    status = ensure_deps_table(root, &deps, error);
    if (status != BASL_STATUS_OK) goto cleanup;

    /* Add/update dependency in [deps] */
    version_str = spec.version ? spec.version : commit;
    status = basl_toml_string_new(NULL, version_str, strlen(version_str), &version_val, error);
    if (status != BASL_STATUS_OK) goto cleanup;

    /* Remove existing entry first, then add new one */
    basl_toml_table_remove(deps, spec.url, NULL);
    status = basl_toml_table_set(deps, spec.url, strlen(spec.url), version_val, error);
    if (status != BASL_STATUS_OK) {
        basl_toml_free(&version_val);
        goto cleanup;
    }

    status = write_project_toml(project_root, root, error);

cleanup:
    basl_pkg_spec_free(&spec);
    basl_pkg_lock_free(&lock);
    basl_toml_free(&root);
    return status;
}

/* ── High-level: basl_pkg_sync ───────────────────────────────────── */

basl_status_t basl_pkg_sync(
    const char *project_root,
    basl_error_t *error
) {
    basl_toml_value_t *root = NULL;
    const basl_toml_value_t *deps;
    basl_pkg_lock_t lock = {0};
    char lock_path[4096];
    size_t i, count;
    basl_status_t status;

    if (project_root == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_pkg_git_available(error);
    if (status != BASL_STATUS_OK) return status;

    status = read_project_toml(project_root, &root, error);
    if (status != BASL_STATUS_OK) return status;

    deps = basl_toml_table_get(root, "deps");
    if (deps == NULL || basl_toml_type(deps) != BASL_TOML_TABLE) {
        basl_toml_free(&root);
        return BASL_STATUS_OK; /* No deps to sync */
    }

    if (basl_platform_path_join(project_root, "basl.lock", lock_path,
            sizeof(lock_path), error) != BASL_STATUS_OK) {
        basl_toml_free(&root);
        return error->type;
    }

    basl_pkg_lock_init(&lock);
    basl_pkg_lock_read(lock_path, &lock, NULL);

    count = basl_toml_table_count(deps);
    for (i = 0; i < count; i++) {
        const char *pkg_url = NULL;
        size_t pkg_url_len;
        const basl_toml_value_t *version_val;
        const char *version = NULL;
        char url_copy[1024];
        char commit[64];

        if (basl_toml_table_entry(deps, i, &pkg_url, &pkg_url_len, &version_val) != BASL_STATUS_OK)
            continue;

        if (version_val != NULL && basl_toml_type(version_val) == BASL_TOML_STRING) {
            version = basl_toml_string_value(version_val);
        }

        snprintf(url_copy, sizeof(url_copy), "%.*s", (int)pkg_url_len, pkg_url);

        /* Install package (handles transitive deps too) */
        status = install_package(project_root, url_copy, version, &lock,
            commit, sizeof(commit), error);
        if (status != BASL_STATUS_OK) {
            basl_toml_free(&root);
            basl_pkg_lock_free(&lock);
            return status;
        }
    }

    basl_pkg_lock_write(lock_path, &lock, error);
    basl_pkg_lock_free(&lock);
    basl_toml_free(&root);
    return BASL_STATUS_OK;
}

/* ── High-level: basl_pkg_remove ─────────────────────────────────── */

static basl_status_t remove_directory_recursive(const char *path, basl_error_t *error);

static basl_status_t remove_dir_callback(const char *name, int is_dir, void *user_data) {
    char *parent = (char *)user_data;
    char child[4096];
    basl_error_t err = {0};

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return BASL_STATUS_OK;

    snprintf(child, sizeof(child), "%s/%s", parent, name);

    if (is_dir) {
        remove_directory_recursive(child, &err);
    } else {
        basl_platform_remove(child, &err);
    }
    return BASL_STATUS_OK;
}

static basl_status_t remove_directory_recursive(const char *path, basl_error_t *error) {
    basl_platform_list_dir(path, remove_dir_callback, (void *)path, error);
    return basl_platform_remove(path, error);
}

basl_status_t basl_pkg_remove(
    const char *project_root,
    const char *package_url,
    basl_error_t *error
) {
    basl_toml_value_t *root = NULL;
    basl_toml_value_t *deps = NULL;
    basl_pkg_lock_t lock = {0};
    char deps_path[4096];
    char pkg_path[4096];
    char lock_path[4096];
    basl_status_t status;
    int pkg_exists = 0;
    size_t i;

    if (project_root == NULL || package_url == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Build paths */
    if (basl_platform_path_join(project_root, "deps", deps_path,
            sizeof(deps_path), error) != BASL_STATUS_OK) {
        return error->type;
    }
    if (basl_platform_path_join(deps_path, package_url, pkg_path,
            sizeof(pkg_path), error) != BASL_STATUS_OK) {
        return error->type;
    }
    if (basl_platform_path_join(project_root, "basl.lock", lock_path,
            sizeof(lock_path), error) != BASL_STATUS_OK) {
        return error->type;
    }

    /* Remove from basl.toml */
    status = read_project_toml(project_root, &root, error);
    if (status != BASL_STATUS_OK) return status;

    deps = (basl_toml_value_t *)basl_toml_table_get(root, "deps");
    if (deps != NULL && basl_toml_type(deps) == BASL_TOML_TABLE) {
        basl_toml_table_remove(deps, package_url, error);
    }

    status = write_project_toml(project_root, root, error);
    basl_toml_free(&root);
    if (status != BASL_STATUS_OK) return status;

    /* Remove from basl.lock */
    basl_pkg_lock_init(&lock);
    if (basl_pkg_lock_read(lock_path, &lock, NULL) == BASL_STATUS_OK) {
        /* Find and remove entry */
        for (i = 0; i < lock.count; i++) {
            if (strcmp(lock.entries[i].name, package_url) == 0) {
                free(lock.entries[i].name);
                free(lock.entries[i].version);
                free(lock.entries[i].commit);
                if (i + 1 < lock.count) {
                    memmove(&lock.entries[i], &lock.entries[i + 1],
                        (lock.count - i - 1) * sizeof(basl_pkg_lock_entry_t));
                }
                lock.count--;
                break;
            }
        }
        basl_pkg_lock_write(lock_path, &lock, error);
    }
    basl_pkg_lock_free(&lock);

    /* Remove from deps/ */
    basl_platform_file_exists(pkg_path, &pkg_exists);
    if (pkg_exists) {
        remove_directory_recursive(pkg_path, error);
    }

    return BASL_STATUS_OK;
}

/* ── Import resolution ───────────────────────────────────────────── */

basl_status_t basl_pkg_resolve_import(
    const char *project_root,
    const char *import_path,
    char *out_path,
    size_t path_size,
    basl_error_t *error
) {
    char deps_path[4096];
    char candidate[4096];
    int exists = 0;

    if (project_root == NULL || import_path == NULL || out_path == NULL) {
        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Only handle imports that look like package URLs (contain a dot before first slash) */
    {
        const char *slash = strchr(import_path, '/');
        const char *dot = strchr(import_path, '.');
        if (dot == NULL || slash == NULL || dot > slash) {
            return BASL_STATUS_INVALID_ARGUMENT; /* Not a package import */
        }
    }

    /* Build candidate path: deps/<import_path>/lib/<last_segment>.basl
       or deps/<import_path>.basl */
    if (basl_platform_path_join(project_root, "deps", deps_path,
            sizeof(deps_path), error) != BASL_STATUS_OK) {
        return error->type;
    }

    /* Try deps/<import_path>.basl first */
    if (basl_platform_path_join(deps_path, import_path, candidate,
            sizeof(candidate), error) != BASL_STATUS_OK) {
        return error->type;
    }
    {
        size_t len = strlen(candidate);
        if (len + 6 < sizeof(candidate)) {
            memcpy(candidate + len, ".basl", 6);
        }
    }
    if (basl_platform_file_exists(candidate, &exists) == BASL_STATUS_OK && exists) {
        if (strlen(candidate) >= path_size) {
            set_error(error, BASL_STATUS_INVALID_ARGUMENT, "path buffer too small");
            return BASL_STATUS_INVALID_ARGUMENT;
        }
        strcpy(out_path, candidate);
        return BASL_STATUS_OK;
    }

    /* Try deps/<package>/lib/<module>.basl */
    {
        /* Find the package root (first 3 segments: domain/user/repo) */
        const char *p = import_path;
        int slashes = 0;
        const char *pkg_end = NULL;
        const char *module_start = NULL;

        while (*p) {
            if (*p == '/') {
                slashes++;
                if (slashes == 3) {
                    pkg_end = p;
                    module_start = p + 1;
                    break;
                }
            }
            p++;
        }

        if (pkg_end != NULL && module_start != NULL && *module_start != '\0') {
            char pkg_root[4096];
            char lib_path[4096];

            snprintf(pkg_root, sizeof(pkg_root), "%.*s", (int)(pkg_end - import_path), import_path);

            if (basl_platform_path_join(deps_path, pkg_root, candidate,
                    sizeof(candidate), error) == BASL_STATUS_OK &&
                basl_platform_path_join(candidate, "lib", lib_path,
                    sizeof(lib_path), error) == BASL_STATUS_OK) {

                snprintf(candidate, sizeof(candidate), "%s/%s.basl", lib_path, module_start);
                if (basl_platform_file_exists(candidate, &exists) == BASL_STATUS_OK && exists) {
                    if (strlen(candidate) >= path_size) {
                        set_error(error, BASL_STATUS_INVALID_ARGUMENT, "path buffer too small");
                        return BASL_STATUS_INVALID_ARGUMENT;
                    }
                    strcpy(out_path, candidate);
                    return BASL_STATUS_OK;
                }
            }
        }
    }

    /* Try deps/<import_path>/lib/mod.basl (package entry point) */
    if (basl_platform_path_join(deps_path, import_path, candidate,
            sizeof(candidate), error) == BASL_STATUS_OK) {
        char entry[4096];
        if (basl_platform_path_join(candidate, "lib", entry,
                sizeof(entry), error) == BASL_STATUS_OK) {
            char mod_path[4096];
            snprintf(mod_path, sizeof(mod_path), "%s/mod.basl", entry);
            if (basl_platform_file_exists(mod_path, &exists) == BASL_STATUS_OK && exists) {
                if (strlen(mod_path) >= path_size) {
                    set_error(error, BASL_STATUS_INVALID_ARGUMENT, "path buffer too small");
                    return BASL_STATUS_INVALID_ARGUMENT;
                }
                strcpy(out_path, mod_path);
                return BASL_STATUS_OK;
            }
        }
    }

    return BASL_STATUS_INVALID_ARGUMENT;
}
