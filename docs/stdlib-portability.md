# Standard Library Portability Guide

This document defines the rules for keeping VIGIL's standard library portable and optional at build time. Follow these guidelines when implementing any stdlib module.

## Principles

1. The core language (lexer, compiler, VM) must compile with only ISO C11 headers.
2. Every stdlib module is a separate compilation unit gated by a CMake option.
3. Platform-specific code lives behind a thin abstraction layer — never in the core.
4. A VIGIL program that only uses tier 1 modules runs identically on every platform.
5. Unsupported modules produce clear compile-time errors in VIGIL, not C build failures.

## Module Tiers

### Tier 1 — Pure C11 (always available)

These modules use only ISO C11 standard library functions and are unconditionally compiled.

| Module   | Backing C APIs                          |
|----------|-----------------------------------------|
| `fmt`    | `snprintf`, `fputs`, `fprintf`          |
| `parse`  | `strtoll`, `strtoull`, `strtod`         |
| `math`   | `<math.h>` functions                    |
| `bytes`  | `memcpy`, `memset`, `memmove`           |

Tier 1 modules must not include any header outside of: `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`, `<limits.h>`, `<math.h>`.

### Tier 2 — Platform abstraction required (on by default)

These modules need OS-specific APIs but are expected to work on most desktop systems. They are enabled by default and can be disabled for embedded or WASM targets.

| Module | Needs                                              |
|--------|----------------------------------------------------|
| `file` | `stat`, `readdir`/`FindFirstFile`, `mkdir`, `unlink` |
| `env`  | `getenv` (C11), `setenv`/`_putenv_s` (platform)   |
| `time` | `time()` (C11), high-res timers (platform)         |

### Tier 3 — Heavily platform-specific (off by default)

These modules depend on APIs that may not exist on all targets. They are opt-in.

| Module    | Needs                        |
|-----------|------------------------------|
| `net`     | BSD sockets / Winsock        |
| `process` | `fork`/`exec` / `CreateProcess` |
| `thread`  | pthreads / Win32 threads     |

## File Layout

```
src/
    stdlib/
        stdlib_fmt.c          # tier 1
        stdlib_parse.c        # tier 1
        stdlib_math.c         # tier 1
        stdlib_bytes.c        # tier 1
        stdlib_file.c         # tier 2
        stdlib_env.c          # tier 2
        stdlib_time.c         # tier 2
        stdlib_net.c          # tier 3
        stdlib_process.c      # tier 3
        stdlib_thread.c       # tier 3
    platform/
        platform.h            # unified C API for OS operations
        platform_posix.c      # Linux, macOS, BSD, etc.
        platform_win32.c      # Windows
        platform_stub.c       # fallback: returns VIGIL_STATUS_UNSUPPORTED
```

Stdlib modules call functions declared in `platform.h`. They never include `<unistd.h>`, `<windows.h>`, or any other platform header directly.

## CMake Integration

### Options

```cmake
# Tier 1 — always built, no option needed

# Tier 2 — on by default
option(VIGIL_STDLIB_FILE    "Build file I/O module"       ON)
option(VIGIL_STDLIB_ENV     "Build env module"            ON)
option(VIGIL_STDLIB_TIME    "Build time module"            ON)

# Tier 3 — off by default
option(VIGIL_STDLIB_NET     "Build networking module"      OFF)
option(VIGIL_STDLIB_PROCESS "Build process module"         OFF)
option(VIGIL_STDLIB_THREAD  "Build thread module"          OFF)
```

### Conditional compilation

```cmake
# Tier 1 — unconditional
target_sources(vigil PRIVATE
    src/stdlib/stdlib_fmt.c
    src/stdlib/stdlib_parse.c
    src/stdlib/stdlib_math.c
    src/stdlib/stdlib_bytes.c
)

# Tier 2 — conditional
if(VIGIL_STDLIB_FILE)
    target_sources(vigil PRIVATE src/stdlib/stdlib_file.c)
    target_compile_definitions(vigil PRIVATE VIGIL_HAS_STDLIB_FILE)
endif()

# Platform layer — selected by OS
if(WIN32)
    target_sources(vigil PRIVATE src/platform/platform_win32.c)
elseif(UNIX)
    target_sources(vigil PRIVATE src/platform/platform_posix.c)
else()
    target_sources(vigil PRIVATE src/platform/platform_stub.c)
endif()
```

### Import resolver gating

When a VIGIL script imports a module, the compiler checks availability:

```c
#ifdef VIGIL_HAS_STDLIB_FILE
    if (name_matches(module_name, "file")) {
        return vigil_stdlib_file_register(program);
    }
#endif
```

If the module was not compiled in, the compiler reports:

```
error: stdlib module 'file' is not available in this build
```

## Platform Abstraction Layer

### Rules

1. `platform.h` declares a flat C API using only ISO C11 types.
2. Each platform implementation file (`platform_posix.c`, `platform_win32.c`) includes its own OS headers internally.
3. `platform_stub.c` implements every function by returning `VIGIL_STATUS_UNSUPPORTED`. This ensures the project always compiles, even on unknown platforms.
4. All platform functions take a `vigil_runtime_t *` so they can use the custom allocator.

### Example

```c
/* platform.h */
#ifndef VIGIL_PLATFORM_H
#define VIGIL_PLATFORM_H

#include "vigil/runtime.h"
#include "vigil/status.h"

vigil_status_t vigil_platform_read_file(
    vigil_runtime_t *runtime,
    const char *path,
    void **out_data,
    size_t *out_length,
    vigil_error_t *error
);

vigil_status_t vigil_platform_write_file(
    vigil_runtime_t *runtime,
    const char *path,
    const void *data,
    size_t length,
    vigil_error_t *error
);

vigil_status_t vigil_platform_file_exists(
    const char *path,
    int *out_exists
);

vigil_status_t vigil_platform_mkdir(
    const char *path,
    vigil_error_t *error
);

#endif
```

```c
/* platform_stub.c */
#include "platform.h"

vigil_status_t vigil_platform_read_file(
    vigil_runtime_t *runtime, const char *path,
    void **out_data, size_t *out_length, vigil_error_t *error
) {
    (void)runtime; (void)path; (void)out_data; (void)out_length;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED,
        "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ... same pattern for all functions ... */
```

## Stdlib Module Implementation Rules

1. **One file per module.** `stdlib_fmt.c` implements everything for `import "fmt"`.
2. **Use the custom allocator.** All allocations go through `vigil_runtime_alloc` / `vigil_runtime_realloc` / `vigil_runtime_free`. Never call `malloc` or `free` directly.
3. **No platform headers in stdlib files.** Call `platform.h` functions instead.
4. **Register functions with the runtime.** Each module exposes a single `vigil_stdlib_<name>_register` function that the import resolver calls.
5. **Return errors, don't crash.** Fallible operations return `(value, err)` tuples following VIGIL conventions.
6. **Keep tier 1 modules dependency-free.** They must not call any platform layer function.

## Testing

- Tier 1 modules are tested in all CI configurations including Emscripten.
- Tier 2 modules are tested on Linux, macOS, and Windows. Emscripten builds disable them.
- Tier 3 modules are tested only on platforms where they are enabled.
- The stub platform layer is tested by building with all tier 2+ modules enabled on a target that uses the stub. This verifies that `VIGIL_STATUS_UNSUPPORTED` is returned correctly.

## Adding a New Module

1. Decide the tier based on what C APIs it needs.
2. Create `src/stdlib/stdlib_<name>.c`.
3. If tier 2+, add any needed functions to `platform.h` and implement them in each platform file (including the stub).
4. Add the CMake option, `target_sources`, and `target_compile_definitions`.
5. Add the `#ifdef` gate in the import resolver.
6. Add tests gated by the same CMake option.
7. Document the module in `docs/stdlib/`.

## Build Configurations

| Target           | Tier 1 | Tier 2 | Tier 3 | Platform layer   |
|------------------|--------|--------|--------|------------------|
| Linux / macOS    | ✓      | ✓      | opt-in | `platform_posix.c` |
| Windows          | ✓      | ✓      | opt-in | `platform_win32.c` |
| Emscripten/WASM  | ✓      | ✗      | ✗      | `platform_stub.c`  |
| Embedded / other | ✓      | opt-in | ✗      | `platform_stub.c`  |

The default `cmake -S . -B build` on a desktop system builds tier 1 + tier 2 with no extra flags. Minimal builds use:

```bash
cmake -S . -B build -DVIGIL_STDLIB_FILE=OFF -DVIGIL_STDLIB_ENV=OFF -DVIGIL_STDLIB_TIME=OFF
```
