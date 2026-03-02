# BASL Limitations Exposed and Fixed

**STATUS: ALL 5 FIXED! ✅**

This document records the language and stdlib limitations discovered while implementing grep, and how they were fixed.

---

## 1. Type Namespace Mismatch - FIXED ✅

**Issue:** Module functions returned unqualified type names.

**Fix:** All stdlib types now use module-qualified names:
- `file.FileStat`, `file.File`, `args.ArgParser`, `args.Result`, `regex.Regex`

**Result:** Type annotations match runtime types.

---

## 2. Subprocess API - FIXED ✅

**Issue:** No way to get exit codes from subprocesses.

**Fix:** Enhanced `os.exec()` and added `os.system()`:
```basl
string out, string err, i32 exitCode, err e = os.exec("cmd", "arg");
string out, string err, i32 exitCode, err e = os.system("shell command");
```

**Result:** Exit codes available. Non-zero exit is NOT an error.

---

## 3. CLI Parsing - FIXED ✅

**Issue:** No short flags, no variadic args, no validation.

**Fix:** Professional `args.Result` API with typed getters:
```basl
args.Result result, err e = parser.parse_result();
string pattern = result.get_string("pattern");
bool verbose = result.get_bool("verbose");
array<string> files = result.get_list("files");  // Native array!
```

**Features:**
- Short flags (`-i`, `-n`)
- Long flags (`--ignore-case`)
- Variadic positionals (native `array<string>`)
- Unknown flag validation
- Missing value validation
- `--` end-of-options support

**Result:** Professional, production-ready CLI parsing.

---

## 4. Compiled Regex - FIXED ✅

**Issue:** Every `regex.match()` recompiled the pattern.

**Fix:** Added `regex.compile()` and `regex.Regex` type:
```basl
regex.Regex re, err e = regex.compile("pattern");
for line in lines {
    bool matches = re.match(line);  // No recompilation!
}
```

**Result:** No O(n) recompilation overhead. Major performance improvement.

---

## 5. API Consistency - FIXED ✅

**Issue:** Inconsistent naming (`file.list_dir` vs `file.read_all`).

**Fix:** Added `file.read_dir()` as alias for `file.list_dir()`.

**Result:** Consistent naming conventions.

---

## Summary

All 5 limitations fixed:

1. ✅ **Type namespacing** - Coherent model for stdlib types
2. ✅ **Subprocess API** - Exit codes, shell execution
3. ✅ **CLI parsing** - Professional API with native arrays
4. ✅ **Compiled regex** - Reusable pattern objects
5. ✅ **API consistency** - Predictable naming

These fixes enable production-ready Unix-tool development in BASL.

## grep Implementation

The grep implementation demonstrates all fixes:
- Uses `args.Result` with typed getters
- Uses `regex.compile()` for performance
- Uses proper type namespacing
- Handles all edge cases (spaces, special chars, etc.)
- Production-ready code

See [README.md](README.md) for usage examples.
