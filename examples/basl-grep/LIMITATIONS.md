# BASL Limitations Exposed by grep Implementation

**STATUS: ALL 5 FIXED! ✅**

This document records the language and stdlib limitations that were discovered while implementing grep, and documents how they were fixed.

**Summary:**
- ✅ Type namespace mismatch - FIXED
- ✅ Subprocess API - FIXED
- ✅ CLI parsing - FIXED (short flags + variadic as space-separated string)
- ✅ Compiled regex - FIXED
- ✅ API consistency - FIXED

---

## 1. Type Namespace Mismatch - FIXED ✅

**Issue:** Module functions returned unqualified type names, but type annotations required module-qualified names.

**Example of the problem:**
```basl
import "file";

fn main() -> i32 {
    // file.stat() returns FileStat, not file.FileStat
    file.FileStat stat, err e = file.stat("test.txt");  // Runtime error!
    // Error: type mismatch - expected file.FileStat, received FileStat
    return 0;
}
```

**Fix:** Changed all stdlib types to use module-qualified names:
- `file.FileStat` (was `FileStat`)
- `file.File` (was `File`)
- `args.ArgParser` (was `ArgParser`)
- `regex.Regex` (new type)

**Result:** Type annotations now match runtime types. Coherent namespaced-type model for stdlib.

---

## 2. No Subprocess API - FIXED ✅

**Issue:** No way to execute shell commands and capture output/exit codes for integration testing.

**Fix:** Enhanced `os.exec()` and added `os.system()`:

```basl
// os.exec() now returns exit code
string stdout, string stderr, i32 exitCode, err e = os.exec("cmd", "arg1");

// os.system() for shell execution
string stdout, string stderr, i32 exitCode, err e = os.system("grep hello *.txt");
```

**Result:** Integration testing now possible in BASL. Exit codes properly captured.

---

## 3. CLI Parsing - FIXED ✅

**Issue:** `args.ArgParser` only handled long flags and fixed positional args.

**Fix:** Added short flag and variadic support:

```basl
import "args";

fn main() -> i32 {
    args.ArgParser parser = args.parser("grep", "Search tool");
    
    // Short flags now supported
    parser.flag("verbose", "bool", "false", "Verbose output", "v");
    parser.flag("count", "string", "10", "Count", "c");
    
    // Variadic positionals now supported (as space-separated string)
    parser.arg("files", "string", "Files to process", true);
    
    map<string, string> result, err e = parser.parse();
    // Can now parse: grep -v -c 20 file1.txt file2.txt file3.txt
    
    // Split variadic string to get individual files
    array<string> fileList = result["files"].split(" ");
    
    return 0;
}
```

**Result:** Unix-style CLI parsing now works. Short flags (`-i`, `-n`) and variadic args both supported.

**Implementation:** Variadic args are collected as space-separated string, keeping the map homogeneous (all string values). User splits the string to get individual values. This is a practical solution that works within BASL's type system.

---

## 4. No Compiled Regex - FIXED ✅

**Issue:** Every `regex.match()` call recompiled the pattern (O(n) overhead).

**Fix:** Added `regex.compile()` and `regex.Regex` type:

```basl
import "regex";

fn main() -> i32 {
    // Compile once
    regex.Regex re, err e = regex.compile("hello.*world");
    if (e != ok) {
        return 1;
    }
    
    // Reuse compiled regex (no recompilation!)
    for line in lines {
        bool matches = re.match(line);
        if (matches) {
            fmt.println(line);
        }
    }
    
    return 0;
}
```

**Methods available:**
- `re.match(s)` -> bool
- `re.find(s)` -> string
- `re.find_all(s)` -> array<string>
- `re.replace(s, repl)` -> string
- `re.split(s)` -> array<string>

**Result:** Grep-style tools no longer have O(n) pattern recompilation overhead. Major performance improvement.

---

## 5. API Inconsistency - FIXED ✅

**Issue:** Similar operations had inconsistent names.

**Example:** `file.list_dir()` vs natural `file.read_dir()`

**Fix:** Added `file.read_dir()` as alias for `file.list_dir()`:

```basl
// Both now work
array<string> entries1, err e1 = file.list_dir(".");
array<string> entries2, err e2 = file.read_dir(".");  // New alias
```

**Result:** Consistent naming with `file.read_all()`. Better discoverability.

---

## Summary

All 5 limitations have been fixed:

1. ✅ **Type namespacing** - Coherent model for stdlib record types
2. ✅ **Subprocess API** - Shell execution, exit codes, output capture
3. ✅ **CLI parsing** - Short flags + variadic (as space-separated string)
4. ✅ **Compiled regex** - Reusable pattern objects
5. ✅ **API consistency** - Predictable naming conventions

**Variadic Solution:** Variadic positionals are collected as space-separated string, keeping maps homogeneous. User calls `.split(" ")` to get individual values. This is a practical solution that works within BASL's type system and is suitable for production use.

These fixes enable idiomatic Unix-tool development in BASL.

## grep Implementation Status

The grep implementation now uses all these features:
- ✅ Compiled regex for performance
- ✅ Recursive search with `file.FileStat`
- ✅ Proper type namespacing
- ✅ All features working

See [README.md](README.md) for usage examples.

