# math

Mathematical functions and constants. All unary functions require `f64` input — passing `i32` is a type error.

```
import "math";
```

## Constants

| Name | Type | Value |
|------|------|-------|
| `math.pi` | `f64` | 3.141592653589793 |
| `math.e` | `f64` | 2.718281828459045 |

## Unary Functions (f64 -> f64)

All of the following accept exactly 1 `f64` argument and return `f64`. Error if argument is not `f64`: `"math.NAME: expected f64 argument"`.

| Function | Description |
|----------|-------------|
| `math.sqrt(x)` | Square root |
| `math.abs(x)` | Absolute value |
| `math.floor(x)` | Round down to nearest integer |
| `math.ceil(x)` | Round up to nearest integer |
| `math.round(x)` | Round to nearest integer (rounds half away from zero) |
| `math.sin(x)` | Sine (radians) |
| `math.cos(x)` | Cosine (radians) |
| `math.tan(x)` | Tangent (radians) |
| `math.log(x)` | Natural logarithm |

```c
f64 r = math.sqrt(4.0);    // 2.0
f64 a = math.abs(-3.5);    // 3.5
f64 f = math.floor(2.7);   // 2.0
f64 c = math.ceil(2.3);    // 3.0
f64 n = math.round(2.5);   // 3.0
```

## Binary Functions

### math.pow(f64 base, f64 exp) -> f64

Returns `base` raised to the power `exp`. Error if args are not `(f64, f64)`: `"math.pow: expected (f64, f64)"`.

```c
f64 p = math.pow(2.0, 10.0);  // 1024.0
```

### math.min(f64 a, f64 b) -> f64

Returns the smaller of two values. Error if args are not `(f64, f64)`: `"math.min: expected (f64, f64)"`.

### math.max(f64 a, f64 b) -> f64

Returns the larger of two values. Error if args are not `(f64, f64)`: `"math.max: expected (f64, f64)"`.

```c
f64 lo = math.min(3.0, 7.0);  // 3.0
f64 hi = math.max(3.0, 7.0);  // 7.0
```

### math.random() -> f64

Returns a pseudo-random `f64` in the range `[0.0, 1.0)`. Uses `math/rand` (not cryptographically secure). For crypto-secure randomness, use the `rand` module.

```c
f64 r = math.random();  // e.g., 0.6046602879796196
```
