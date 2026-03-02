# BASL Limitations Exposed by grep Implementation

This document records the real language and stdlib limitations discovered while implementing a grep tool in BASL.

## 1. Type Namespace Mismatch for Stdlib Types

**Issue:** Module functions return unqualified type names, but type annotations require module-qualified names.

**Example:**
```basl
import "file";

fn main() -> i32 {
    // file.stat() returns FileStat, not file.FileStat
    file.FileStat stat, err e = file.stat("test.txt");  // Runtime error!
    // Error: type mismatch - expected file.FileStat, received FileStat
    
    // Workaround: Use unqualified name (but this is inconsistent)
    FileStat stat, err e = file.stat("test.txt");  // Works, but confusing
    return 0;
}
```

**Impact:** Makes it unclear how to correctly type stdlib record types. The natural spelling `file.FileStat` fails at runtime.

**Affected APIs:**
- `file.stat()` returns `FileStat`
- Any stdlib function returning custom types

**Status:** Language design issue - no coherent namespaced-type story for stdlib.

---

## 2. No Subprocess API for Integration Testing

**Issue:** No way to execute shell commands and capture output/exit codes for black-box CLI testing.

**Missing APIs:**
- `os.system(cmd)` - Execute shell command, return exit code
- `os.exec()` with stdin injection
- Structured subprocess result (stdout, stderr, exit code)

**Current State:**
- `os.exec()` exists in docs but not in interpreter (`stdlib_os.go:62`)
- No shell-mode execution
- No stdin injection
- No structured exit-code return

**Example of what's needed:**
```basl
import "os";

fn main() -> i32 {
    // Desired API (doesn't exist):
    i32 exitCode = os.system("grep hello test.txt");
    
    // Or more structured:
    os.ExecResult result, err e = os.exec_capture("grep", ["hello", "test.txt"]);
    string stdout = result.stdout;
    string stderr = result.stderr;
    i32 code = result.exit_code;
    
    return 0;
}
```

**Impact:** Cannot write integration tests for CLI tools in BASL itself. Must use external test harness.

**Status:** Stdlib gap - no subprocess primitives for testing.

---

## 3. Weak CLI Argument Parsing

**Issue:** `args.ArgParser` only handles long flags and fixed positional args. No short flags, no variadic positionals, no unknown-flag validation.

**Current Limitations:**
- No short flag support (`-i`, `-n`, `-v`)
- No variadic positional capture (`[FILE...]`)
- No unknown-flag error handling
- Only `--long` flags supported (`stdlib_args.go:88`, `args.md:144`)

**Example:**
```basl
import "args";

fn main() -> i32 {
    // Cannot parse: grep -i -n pattern file1.txt file2.txt
    // ArgParser doesn't support:
    // - Short flags (-i, -n)
    // - Variable number of files
    
    // Current workaround: manual os.args() parsing
    array<string> argv = os.args();
    // ... manual flag parsing loop ...
    
    return 0;
}
```

**Impact:** Unix-style tools must manually parse `os.args()`, defeating the purpose of the `args` module.

**Status:** Stdlib gap - args parser too limited for real CLI tools.

---

## 4. No Compiled Regex Objects

**Issue:** Every `regex.match()` call recompiles the pattern. No way to compile once and reuse.

**Current Behavior:**
```go
// stdlib_regex.go:12
func(args []value.Value) (value.Value, error) {
    pattern := args[0].AsString()
    text := args[1].AsString()
    matched, err := regexp.MatchString(pattern, text)  // Recompiles every time!
    // ...
}
```

**Performance Impact:**
```basl
// grep searches 1000 lines with same pattern
for (i32 i = 0; i < lines.len(); i++) {
    bool matches, err e = regex.match(pattern, lines[i]);  // Recompiles 1000 times!
}
```

**Desired API:**
```basl
import "regex";

fn main() -> i32 {
    // Compile once
    regex.Regex re, err e = regex.compile("hello.*world");
    if (e != ok) {
        return 1;
    }
    
    // Reuse compiled regex
    for line in lines {
        bool matches = re.match(line);  // No recompilation
    }
    
    return 0;
}
```

**Impact:** Grep-style tools have O(n) pattern recompilation overhead. Major performance issue for line-by-line matching.

**Status:** Stdlib limitation - no regex object type, all functions recompile.

---

## 5. API Naming Inconsistency

**Issue:** Similar operations have inconsistent names across modules.

**Example:**
- `file.list_dir()` - lists directory entries
- But natural name would be `file.read_dir()` (matching `file.read_all()`)

**Impact:** Developers must memorize arbitrary naming choices. Reduces discoverability.

**Status:** Stdlib design inconsistency.

---

## Summary

For Unix-tool style development, BASL currently lacks:

1. **Type namespacing** - Coherent model for stdlib record types
2. **Subprocess API** - Shell execution, exit codes, output capture
3. **CLI parsing** - Short flags, variadic args, validation
4. **Compiled regex** - Reusable pattern objects
5. **API consistency** - Predictable naming conventions

These are not implementation bugs in the grep example - they are genuine language/stdlib gaps that prevent idiomatic Unix-tool development in BASL.
