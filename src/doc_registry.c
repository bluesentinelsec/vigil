#include "basl/doc_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/basl_internal.h"

/* ── Builtin Function Docs ────────────────────────────────── */

static const basl_doc_entry_t builtin_docs[] = {
    {
        "builtins",
        NULL,
        "Built-in functions available without import.",
        "These functions are always available in BASL programs.",
        NULL
    },
    {
        "len",
        "len(value: string | array | map) -> int",
        "Return the length of a string, array, or map.",
        NULL,
        "len(\"hello\")  // 5\nlen([1, 2, 3])  // 3"
    },
    {
        "type",
        "type(value: any) -> string",
        "Return the type name of a value.",
        NULL,
        "type(42)       // \"int\"\ntype(\"hello\")  // \"string\""
    },
    {
        "str",
        "str(value: any) -> string",
        "Convert a value to its string representation.",
        NULL,
        "str(42)    // \"42\"\nstr(true)  // \"true\""
    },
    {
        "int",
        "int(value: string | float) -> int",
        "Convert a string or float to an integer.",
        NULL,
        "int(\"42\")   // 42\nint(3.14)   // 3"
    },
    {
        "float",
        "float(value: string | int) -> float",
        "Convert a string or integer to a float.",
        NULL,
        "float(\"3.14\")  // 3.14\nfloat(42)      // 42.0"
    },
    {
        "exit",
        "exit(code: int) -> void",
        "Exit the program with the given status code.",
        NULL,
        "exit(0)  // success\nexit(1)  // failure"
    },
    {
        "char",
        "char(code: int) -> string",
        "Convert a byte value (0-255) to a single-character string.",
        NULL,
        "char(65)   // \"A\"\nchar(0x0a) // \"\\n\""
    },
};

#define BUILTIN_COUNT (sizeof(builtin_docs) / sizeof(builtin_docs[0]))

/* ── fmt Module Docs ──────────────────────────────────────── */

static const basl_doc_entry_t fmt_docs[] = {
    {
        "fmt",
        NULL,
        "Formatted output functions.",
        "The fmt module provides functions for printing to stdout and stderr.",
        NULL
    },
    {
        "fmt.print",
        "fmt.print(value: string) -> void",
        "Print a string to stdout without a newline.",
        NULL,
        "fmt.print(\"hello \")\nfmt.print(\"world\")"
    },
    {
        "fmt.println",
        "fmt.println(value: string) -> void",
        "Print a string to stdout with a newline.",
        NULL,
        "fmt.println(\"Hello, world!\")"
    },
    {
        "fmt.eprintln",
        "fmt.eprintln(value: string) -> void",
        "Print a string to stderr with a newline.",
        NULL,
        "fmt.eprintln(\"Error: something went wrong\")"
    },
};

#define FMT_COUNT (sizeof(fmt_docs) / sizeof(fmt_docs[0]))

/* ── math Module Docs ─────────────────────────────────────── */

static const basl_doc_entry_t math_docs[] = {
    {
        "math",
        NULL,
        "Mathematical functions and constants.",
        "The math module provides common mathematical operations.",
        NULL
    },
    {
        "math.sqrt",
        "math.sqrt(x: float) -> float",
        "Return the square root of x.",
        NULL,
        "math.sqrt(16.0)  // 4.0"
    },
    {
        "math.abs",
        "math.abs(x: int | float) -> int | float",
        "Return the absolute value of x.",
        NULL,
        "math.abs(-5)    // 5\nmath.abs(-3.14) // 3.14"
    },
    {
        "math.floor",
        "math.floor(x: float) -> int",
        "Return the largest integer less than or equal to x.",
        NULL,
        "math.floor(3.7)   // 3\nmath.floor(-3.2)  // -4"
    },
    {
        "math.ceil",
        "math.ceil(x: float) -> int",
        "Return the smallest integer greater than or equal to x.",
        NULL,
        "math.ceil(3.2)   // 4\nmath.ceil(-3.7)  // -3"
    },
    {
        "math.pow",
        "math.pow(base: float, exp: float) -> float",
        "Return base raised to the power exp.",
        NULL,
        "math.pow(2.0, 10.0)  // 1024.0"
    },
    {
        "math.sin",
        "math.sin(x: float) -> float",
        "Return the sine of x (in radians).",
        NULL,
        "math.sin(0.0)  // 0.0"
    },
    {
        "math.cos",
        "math.cos(x: float) -> float",
        "Return the cosine of x (in radians).",
        NULL,
        "math.cos(0.0)  // 1.0"
    },
    {
        "math.tan",
        "math.tan(x: float) -> float",
        "Return the tangent of x (in radians).",
        NULL,
        "math.tan(0.0)  // 0.0"
    },
    {
        "math.log",
        "math.log(x: float) -> float",
        "Return the natural logarithm of x.",
        NULL,
        "math.log(2.718281828)  // ~1.0"
    },
    {
        "math.exp",
        "math.exp(x: float) -> float",
        "Return e raised to the power x.",
        NULL,
        "math.exp(1.0)  // ~2.718"
    },
};

#define MATH_COUNT (sizeof(math_docs) / sizeof(math_docs[0]))

/* ── args Module Docs ─────────────────────────────────────── */

static const basl_doc_entry_t args_docs[] = {
    {
        "args",
        NULL,
        "Command-line argument access.",
        "The args module provides access to command-line arguments.",
        NULL
    },
    {
        "args.get",
        "args.get() -> [string]",
        "Return the command-line arguments as an array of strings.",
        NULL,
        "let argv = args.get()\nfor arg in argv { println(arg) }"
    },
};

#define ARGS_COUNT (sizeof(args_docs) / sizeof(args_docs[0]))

/* ── test Module Docs ─────────────────────────────────────── */

static const basl_doc_entry_t test_docs[] = {
    {
        "test",
        NULL,
        "Testing utilities.",
        "The test module provides assertion functions for testing.",
        NULL
    },
    {
        "test.assert",
        "test.assert(condition: bool) -> void",
        "Assert that condition is true, panic otherwise.",
        NULL,
        "test.assert(1 + 1 == 2)"
    },
    {
        "test.assert_eq",
        "test.assert_eq(left: any, right: any) -> void",
        "Assert that left equals right, panic otherwise.",
        NULL,
        "test.assert_eq(add(1, 2), 3)"
    },
};

#define TEST_COUNT (sizeof(test_docs) / sizeof(test_docs[0]))

/* ── strings Module Docs ──────────────────────────────────── */

static const basl_doc_entry_t strings_docs[] = {
    {
        "strings",
        NULL,
        "String methods available on all string values.",
        "These methods are called on string values using dot notation.",
        NULL
    },
    {
        "strings.len",
        "s.len() -> i32",
        "Return the length of the string in bytes.",
        NULL,
        "\"hello\".len()  // 5"
    },
    {
        "strings.contains",
        "s.contains(sub: string) -> bool",
        "Return true if s contains the substring sub.",
        NULL,
        "\"hello\".contains(\"ell\")  // true"
    },
    {
        "strings.starts_with",
        "s.starts_with(prefix: string) -> bool",
        "Return true if s starts with prefix.",
        NULL,
        "\"hello\".starts_with(\"he\")  // true"
    },
    {
        "strings.ends_with",
        "s.ends_with(suffix: string) -> bool",
        "Return true if s ends with suffix.",
        NULL,
        "\"hello\".ends_with(\"lo\")  // true"
    },
    {
        "strings.trim",
        "s.trim() -> string",
        "Return s with leading and trailing whitespace removed.",
        NULL,
        "\"  hello  \".trim()  // \"hello\""
    },
    {
        "strings.trim_left",
        "s.trim_left() -> string",
        "Return s with leading whitespace removed.",
        NULL,
        "\"  hello\".trim_left()  // \"hello\""
    },
    {
        "strings.trim_right",
        "s.trim_right() -> string",
        "Return s with trailing whitespace removed.",
        NULL,
        "\"hello  \".trim_right()  // \"hello\""
    },
    {
        "strings.trim_prefix",
        "s.trim_prefix(prefix: string) -> string",
        "Return s without the leading prefix if present.",
        NULL,
        "\"hello\".trim_prefix(\"he\")  // \"llo\""
    },
    {
        "strings.trim_suffix",
        "s.trim_suffix(suffix: string) -> string",
        "Return s without the trailing suffix if present.",
        NULL,
        "\"hello\".trim_suffix(\"lo\")  // \"hel\""
    },
    {
        "strings.to_upper",
        "s.to_upper() -> string",
        "Return s with all ASCII letters converted to uppercase.",
        NULL,
        "\"Hello\".to_upper()  // \"HELLO\""
    },
    {
        "strings.to_lower",
        "s.to_lower() -> string",
        "Return s with all ASCII letters converted to lowercase.",
        NULL,
        "\"Hello\".to_lower()  // \"hello\""
    },
    {
        "strings.replace",
        "s.replace(old: string, new: string) -> string",
        "Return s with all occurrences of old replaced by new.",
        NULL,
        "\"hello\".replace(\"l\", \"L\")  // \"heLLo\""
    },
    {
        "strings.split",
        "s.split(sep: string) -> array<string>",
        "Split s by separator and return an array of substrings.",
        NULL,
        "\"a,b,c\".split(\",\")  // [\"a\", \"b\", \"c\"]"
    },
    {
        "strings.index_of",
        "s.index_of(sub: string) -> (i32, bool)",
        "Return the index of the first occurrence of sub, or (-1, false) if not found.",
        NULL,
        "i32 idx, bool found = \"hello\".index_of(\"l\")  // 2, true"
    },
    {
        "strings.last_index_of",
        "s.last_index_of(sub: string) -> (i32, bool)",
        "Return the index of the last occurrence of sub, or (-1, false) if not found.",
        NULL,
        "i32 idx, bool found = \"hello\".last_index_of(\"l\")  // 3, true"
    },
    {
        "strings.substr",
        "s.substr(start: i32, len: i32) -> (string, err)",
        "Return a substring starting at start with length len.",
        NULL,
        "string sub, err e = \"hello\".substr(1, 3)  // \"ell\""
    },
    {
        "strings.char_at",
        "s.char_at(i: i32) -> (string, err)",
        "Return the character at index i as a single-character string.",
        NULL,
        "string c, err e = \"hello\".char_at(0)  // \"h\""
    },
    {
        "strings.bytes",
        "s.bytes() -> array<u8>",
        "Return the raw bytes of the string as an array.",
        NULL,
        "\"AB\".bytes()  // [65, 66]"
    },
    {
        "strings.reverse",
        "s.reverse() -> string",
        "Return s with characters in reverse order.",
        NULL,
        "\"hello\".reverse()  // \"olleh\""
    },
    {
        "strings.is_empty",
        "s.is_empty() -> bool",
        "Return true if s has length zero.",
        NULL,
        "\"\".is_empty()  // true"
    },
    {
        "strings.repeat",
        "s.repeat(n: i32) -> string",
        "Return s repeated n times.",
        NULL,
        "\"ab\".repeat(3)  // \"ababab\""
    },
    {
        "strings.count",
        "s.count(sub: string) -> i32",
        "Return the number of non-overlapping occurrences of sub in s.",
        NULL,
        "\"banana\".count(\"a\")  // 3"
    },
    {
        "strings.fields",
        "s.fields() -> array<string>",
        "Split s on whitespace and return non-empty fields.",
        "Similar to Go's strings.Fields. Splits on runs of whitespace.",
        "\"  a  b  c  \".fields()  // [\"a\", \"b\", \"c\"]"
    },
    {
        "strings.join",
        "sep.join(arr: array<string>) -> string",
        "Join array elements with sep as separator.",
        "The separator is the receiver, the array is the argument.",
        "\",\".join([\"a\", \"b\", \"c\"])  // \"a,b,c\""
    },
    {
        "strings.cut",
        "s.cut(sep: string) -> (string, string, bool)",
        "Cut s around the first instance of sep.",
        "Returns (before, after, found). If sep is not found, returns (s, \"\", false).",
        "string k, string v, bool ok = \"key=val\".cut(\"=\")  // \"key\", \"val\", true"
    },
    {
        "strings.equal_fold",
        "s.equal_fold(t: string) -> bool",
        "Return true if s equals t under case-insensitive comparison.",
        "Compares ASCII letters case-insensitively.",
        "\"Go\".equal_fold(\"go\")  // true"
    },
};

#define STRINGS_COUNT (sizeof(strings_docs) / sizeof(strings_docs[0]))

/* ── regex Module Docs ────────────────────────────────────── */

static const basl_doc_entry_t regex_docs[] = {
    {
        "regex",
        NULL,
        "Regular expression matching (RE2-style).",
        "The regex module provides pattern matching with linear time guarantees.\n"
        "Uses Thompson NFA algorithm - no backtracking, no pathological cases.",
        NULL
    },
    {
        "regex.match",
        "regex.match(pattern: string, input: string) -> bool",
        "Check if input matches the pattern (anchored).",
        "Returns true if the entire input matches the pattern.",
        "regex.match(\"[a-z]+\", \"hello\")  // true\n"
        "regex.match(\"[a-z]+\", \"hello123\")  // false"
    },
    {
        "regex.find",
        "regex.find(pattern: string, input: string) -> (string, bool)",
        "Find first match of pattern in input.",
        "Returns the matched substring and whether a match was found.",
        "string m, bool ok = regex.find(\"[0-9]+\", \"abc123\")  // \"123\", true"
    },
    {
        "regex.find_all",
        "regex.find_all(pattern: string, input: string) -> array<string>",
        "Find all non-overlapping matches.",
        "Returns an array of all matched substrings.",
        "regex.find_all(\"[0-9]+\", \"a1b22c333\")  // [\"1\", \"22\", \"333\"]"
    },
    {
        "regex.replace",
        "regex.replace(pattern: string, input: string, replacement: string) -> string",
        "Replace first match with replacement.",
        "Returns the input with the first match replaced.",
        "regex.replace(\"[0-9]+\", \"a1b2\", \"X\")  // \"aXb2\""
    },
    {
        "regex.replace_all",
        "regex.replace_all(pattern: string, input: string, replacement: string) -> string",
        "Replace all matches with replacement.",
        "Returns the input with all matches replaced.",
        "regex.replace_all(\"[0-9]+\", \"a1b2\", \"X\")  // \"aXbX\""
    },
    {
        "regex.split",
        "regex.split(pattern: string, input: string) -> array<string>",
        "Split input by pattern.",
        "Returns an array of substrings split by the pattern.",
        "regex.split(\",\", \"a,b,c\")  // [\"a\", \"b\", \"c\"]"
    },
};

#define REGEX_COUNT (sizeof(regex_docs) / sizeof(regex_docs[0]))

/* ── random Module Docs ───────────────────────────────────── */

static const basl_doc_entry_t random_docs[] = {
    {
        "random",
        NULL,
        "Random number generation.",
        "The random module provides pseudo-random number generation\n"
        "using xorshift128+ algorithm.",
        NULL
    },
    {
        "random.seed",
        "random.seed(n: i32)",
        "Seed the random number generator.",
        "Sets the seed for reproducible sequences.",
        "random.seed(42)"
    },
    {
        "random.i64",
        "random.i64() -> i64",
        "Generate a random 64-bit integer.",
        "Returns a random i64 value.",
        "i64 n = random.i64()"
    },
    {
        "random.i32",
        "random.i32() -> i32",
        "Generate a random 32-bit integer.",
        "Returns a random i32 value.",
        "i32 n = random.i32()"
    },
    {
        "random.f64",
        "random.f64() -> f64",
        "Generate a random float in [0, 1).",
        "Returns a random f64 value between 0 (inclusive) and 1 (exclusive).",
        "f64 x = random.f64()  // e.g. 0.7234..."
    },
    {
        "random.range",
        "random.range(min: i32, max: i32) -> i32",
        "Generate a random integer in [min, max).",
        "Returns a random i32 value between min (inclusive) and max (exclusive).",
        "i32 dice = random.range(1, 7)  // 1-6"
    },
};

#define RANDOM_COUNT (sizeof(random_docs) / sizeof(random_docs[0]))

/* ── Module List ──────────────────────────────────────────── */

static const char *module_names[] = {
    "builtins",
    "fmt",
    "math",
    "args",
    "test",
    "strings",
    "regex",
    "random",
};

#define MODULE_COUNT (sizeof(module_names) / sizeof(module_names[0]))

/* ── Lookup Implementation ────────────────────────────────── */

const basl_doc_entry_t *basl_doc_lookup(const char *name) {
    size_t i, len;

    if (name == NULL) return NULL;
    len = strlen(name);

    /* Check builtins */
    for (i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(builtin_docs[i].name, name) == 0) {
            return &builtin_docs[i];
        }
    }

    /* Check fmt */
    for (i = 0; i < FMT_COUNT; i++) {
        if (strcmp(fmt_docs[i].name, name) == 0) {
            return &fmt_docs[i];
        }
    }

    /* Check math */
    for (i = 0; i < MATH_COUNT; i++) {
        if (strcmp(math_docs[i].name, name) == 0) {
            return &math_docs[i];
        }
    }

    /* Check args */
    for (i = 0; i < ARGS_COUNT; i++) {
        if (strcmp(args_docs[i].name, name) == 0) {
            return &args_docs[i];
        }
    }

    /* Check test */
    for (i = 0; i < TEST_COUNT; i++) {
        if (strcmp(test_docs[i].name, name) == 0) {
            return &test_docs[i];
        }
    }

    /* Check strings */
    for (i = 0; i < STRINGS_COUNT; i++) {
        if (strcmp(strings_docs[i].name, name) == 0) {
            return &strings_docs[i];
        }
    }

    /* Check regex */
    for (i = 0; i < REGEX_COUNT; i++) {
        if (strcmp(regex_docs[i].name, name) == 0) {
            return &regex_docs[i];
        }
    }

    /* Check random */
    for (i = 0; i < RANDOM_COUNT; i++) {
        if (strcmp(random_docs[i].name, name) == 0) {
            return &random_docs[i];
        }
    }

    (void)len;
    return NULL;
}

const char **basl_doc_list_modules(size_t *count) {
    if (count != NULL) {
        *count = MODULE_COUNT;
    }
    return module_names;
}

const basl_doc_entry_t *basl_doc_list_module(
    const char *module_name,
    size_t *count
) {
    if (module_name == NULL) return NULL;

    if (strcmp(module_name, "builtins") == 0) {
        if (count) *count = BUILTIN_COUNT;
        return builtin_docs;
    }
    if (strcmp(module_name, "fmt") == 0) {
        if (count) *count = FMT_COUNT;
        return fmt_docs;
    }
    if (strcmp(module_name, "math") == 0) {
        if (count) *count = MATH_COUNT;
        return math_docs;
    }
    if (strcmp(module_name, "args") == 0) {
        if (count) *count = ARGS_COUNT;
        return args_docs;
    }
    if (strcmp(module_name, "test") == 0) {
        if (count) *count = TEST_COUNT;
        return test_docs;
    }
    if (strcmp(module_name, "strings") == 0) {
        if (count) *count = STRINGS_COUNT;
        return strings_docs;
    }
    if (strcmp(module_name, "regex") == 0) {
        if (count) *count = REGEX_COUNT;
        return regex_docs;
    }
    if (strcmp(module_name, "random") == 0) {
        if (count) *count = RANDOM_COUNT;
        return random_docs;
    }

    return NULL;
}

basl_status_t basl_doc_entry_render(
    const basl_doc_entry_t *entry,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
) {
    char *buf;
    size_t len = 0;
    size_t cap = 1024;

    if (entry == NULL || out_text == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "doc: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    buf = malloc(cap);
    if (buf == NULL) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY, "out of memory");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    /* Name/signature */
    if (entry->signature != NULL) {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->signature);
    } else {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->name);
    }

    /* Summary */
    if (entry->summary != NULL) {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n", entry->summary);
    }

    /* Description */
    if (entry->description != NULL) {
        len += (size_t)snprintf(buf + len, cap - len, "\n%s\n", entry->description);
    }

    /* Example */
    if (entry->example != NULL) {
        len += (size_t)snprintf(buf + len, cap - len, "\nExample:\n  %s\n",
                                entry->example);
    }

    *out_text = buf;
    if (out_length) *out_length = len;
    return BASL_STATUS_OK;
}
