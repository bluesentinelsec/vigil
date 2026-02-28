# BASL `doc` Command

`basl doc` extracts and displays documentation from BASL source files. It shows public API documentation including functions, classes, constants, and their doc comments.

## Usage

```bash
basl doc <file.basl>           # Show all public symbols
basl doc <file.basl> <symbol>  # Show specific symbol
```

## Examples

### Show module documentation

```bash
basl doc lib/utils.basl
```

Output:
```
MODULE
  utils

SUMMARY
  Utility functions for string manipulation

FUNCTIONS
  pub fn trim(string s) -> string
  pub fn split(string s, string sep) -> array<string>
```

### Show specific symbol

```bash
basl doc lib/utils.basl trim
```

Output:
```
FUNCTION
  pub fn trim(string s) -> string

DESCRIPTION
  Removes leading and trailing whitespace from a string.
```

## Documentation Comments

`basl doc` extracts documentation from comments that appear immediately before declarations:

### Function documentation

```c
// Calculates the factorial of n.
// Returns 1 for n <= 1.
pub fn factorial(i32 n) -> i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
```

### Class documentation

```c
// Represents a 2D point with x and y coordinates.
pub class Point {
    pub i32 x;
    pub i32 y;
    
    // Creates a new point at the origin.
    fn init() -> void {
        self.x = 0;
        self.y = 0;
    }
    
    // Calculates the distance from the origin.
    pub fn distance() -> f64 {
        return math.sqrt(f64(self.x * self.x + self.y * self.y));
    }
}
```

### Module-level documentation

```c
// Package utils provides string and array manipulation utilities.
// 
// This module is designed for common data processing tasks.

pub fn trim(string s) -> string {
    // ...
}
```

## What Gets Documented

`basl doc` shows only **public** (`pub`) declarations:

- ✅ Public functions
- ✅ Public classes and their public methods
- ✅ Public constants
- ✅ Public variables
- ✅ Public enums
- ✅ Public interfaces
- ❌ Private (non-pub) declarations

## Output Format

### Module view (no symbol specified)

Shows:
1. Module name (derived from filename)
2. Module summary (from leading comments)
3. All public symbols organized by type

### Symbol view (specific symbol)

Shows:
1. Symbol signature
2. Documentation comments
3. For classes: all public methods
4. For interfaces: all method signatures

## Class Member Documentation

View a specific class method:

```bash
basl doc lib/point.basl Point.distance
```

## Use Cases

### Library authors

Document your public API:

```bash
# Generate docs for all library modules
for f in lib/*.basl; do
    echo "=== $(basename $f) ==="
    basl doc "$f"
    echo
done
```

### Library users

Explore available functions:

```bash
# See what's available in a module
basl doc deps/mylib/lib/http.basl

# Get details on a specific function
basl doc deps/mylib/lib/http.basl request
```

### Code review

Check public API surface:

```bash
basl doc lib/api.basl
```

## Best Practices

1. **Document all public symbols** - Add comments above every `pub` declaration

2. **Use clear descriptions** - Explain what the function does, not how

3. **Document parameters** - Mention parameter constraints or special values

4. **Document return values** - Explain what the function returns

5. **Include examples in comments** when helpful:
   ```c
   // Splits a string by delimiter.
   // Example: split("a,b,c", ",") returns ["a", "b", "c"]
   pub fn split(string s, string sep) -> array<string> {
       // ...
   }
   ```

## Integration with Other Tools

### With `basl test`

Document test behavior:

```c
// Tests that factorial(0) returns 1
fn test_factorial_zero() -> void {
    t.assert(factorial(0) == 1, "factorial(0) should be 1");
}
```

### With `basl package`

Documentation is preserved in packaged libraries, so users can run `basl doc` on bundled code.

## Limitations

- Only extracts comments immediately before declarations
- Does not parse structured doc comment formats (like JSDoc or Javadoc)
- Does not generate HTML or other formatted output
- Does not cross-reference between modules

## Future Enhancements

Potential future features:
- HTML documentation generation
- Cross-module documentation
- Structured doc comment parsing
- Example code extraction and testing
