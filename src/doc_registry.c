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

/* ── Module List ──────────────────────────────────────────── */

static const char *module_names[] = {
    "builtins",
    "fmt",
    "math",
    "args",
    "test",
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
