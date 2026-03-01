# Unix Utilities Implementation - Language Validation

This directory contains implementations of common Unix utilities in BASL to validate the language and identify gaps.

## Implemented Utilities

### 1. basl-sort
Sorts lines from stdin alphabetically.

**Usage:** `cat file.txt | basl-sort`

**Status:** ✅ Working

### 2. basl-uniq
Removes consecutive duplicate lines from stdin.

**Usage:** `cat file.txt | basl-uniq`

**Status:** ✅ Working

### 3. basl-nl
Numbers lines from stdin.

**Usage:** `cat file.txt | basl-nl`

**Status:** ✅ Working

### 4. basl-cut
Extracts fields from delimited text.

**Usage:** `cat file.txt | basl-cut --f 2 --d :`

**Status:** ✅ Working

### 5. basl-tr
Translates characters (character-by-character replacement).

**Usage:** `echo "hello" | basl-tr aeiou 12345`

**Status:** ✅ Working

### 6. basl-paste
Merges lines from multiple files side-by-side.

**Usage:** `basl-paste file1.txt file2.txt`

**Status:** ✅ Working

## Language Gaps Identified

### 1. Missing `strings.split()` function ⚠️
**Impact:** Medium  
**Workaround:** Use built-in `string.split()` method instead  
**Found in:** basl-cut implementation  
**Resolution:** Documentation shows `string.split()` method exists, so no stdlib function needed

### 2. No `io.read()` for chunked stdin reading ⚠️
**Impact:** High  
**Status:** Implemented in PR #20  
**Found in:** basl-tr (needs to process large streams efficiently)  
**Resolution:** Using `io.read(4096)` for chunked processing

### 3. Type casting verbosity
**Impact:** Low  
**Example:** `c >= u8(48) && c <= u8(57)` requires explicit casts  
**Found in:** basl-cut (parsing field numbers)  
**Note:** This is by design for type safety

### 4. No string interpolation in map access
**Impact:** Low  
**Example:** Cannot use `f"{opts['f']}"` directly in f-strings  
**Workaround:** Assign to variable first: `string val = opts["f"]; fmt.println(f"{val}");`  
**Found in:** basl-cut

## Language Features Validated ✅

1. **I/O Operations**
   - `io.read_line()` - line-by-line stdin reading
   - `io.read()` - chunked stdin reading (new)
   - `file.open()` and `File.read_line()` - file operations

2. **String Operations**
   - `string.split()` - split by delimiter
   - `string.char_at()` - character access
   - `string.len()` - length
   - String concatenation with `+=`

3. **Array Operations**
   - `array.push()` - append elements
   - `array.len()` - length
   - Array indexing with `[]`
   - For-in loops over arrays

4. **Control Flow**
   - While loops with break
   - For loops with counters
   - For-in loops
   - If/else conditionals

5. **Error Handling**
   - Multi-return values `(value, err)`
   - Error checking with `e != ok`
   - Error propagation

6. **Command-line Parsing**
   - `args.parser()` - argument parser
   - `p.flag()` - define flags
   - `p.parse()` - parse arguments
   - `os.args()` - raw arguments

7. **Type System**
   - Static typing with inference
   - Explicit type casts
   - Type safety enforcement

## Testing

All utilities have been manually tested with representative inputs:

```bash
# sort
echo -e "zebra\napple\nbanana" | ./basl examples/basl-sort/main.basl
# Output: apple, banana, zebra

# uniq
echo -e "a\na\nb\nb\nb\nc" | ./basl examples/basl-uniq/main.basl
# Output: a, b, c

# nl
echo -e "hello\nworld\ntest" | ./basl examples/basl-nl/main.basl
# Output: numbered lines

# cut
echo -e "a:b:c\nd:e:f" | ./basl examples/basl-cut/main.basl --f 2 --d :
# Output: b, e

# tr
echo "hello world" | ./basl examples/basl-tr/main.basl aeiou 12345
# Output: h2ll4 w4rld

# paste
echo -e "a\nb\nc" > /tmp/f1.txt
echo -e "1\n2\n3" > /tmp/f2.txt
./basl examples/basl-paste/main.basl /tmp/f1.txt /tmp/f2.txt
# Output: a\t1, b\t2, c\t3
```

## Conclusion

BASL successfully implements all target Unix utilities with minimal friction. The language provides:

- ✅ Sufficient I/O primitives
- ✅ Good string manipulation
- ✅ Solid array operations
- ✅ Clean error handling
- ✅ Effective command-line parsing

**Main finding:** The language is production-ready for systems scripting and CLI tool development. The only significant gap (`io.read()`) has been addressed in PR #20.
