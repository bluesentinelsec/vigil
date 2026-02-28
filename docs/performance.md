# BASL Performance

BASL is an interpreted scripting language implemented in Go. This document provides performance characteristics and guidance for understanding BASL's performance profile.

## Performance Profile

BASL is designed for:
- **Rapid prototyping** - Fast development iteration
- **Scripting and automation** - System administration, build scripts, data processing
- **Embedded scripting** - Extending Go applications with user-defined logic
- **CLI tools** - Command-line utilities with reasonable performance

BASL is **not** designed for:
- CPU-intensive numerical computation
- Real-time systems requiring microsecond latency
- High-frequency trading or game engines
- Replacing compiled languages in performance-critical paths

## Execution Model

- **Tree-walking interpreter** - Executes AST directly, no bytecode compilation
- **Runtime type checking** - Type enforcement happens at runtime
- **Go runtime** - Benefits from Go's garbage collector and goroutine scheduler
- **Native stdlib** - Core operations (I/O, HTTP, SQLite) use native Go implementations

## Expected Performance Characteristics

As an interpreted language, BASL will generally be:
- **10-100x slower than compiled Go** for CPU-bound operations
- **Similar to Python** for interpreted workloads
- **Near-native speed** for I/O-bound operations using stdlib (HTTP, file I/O, JSON, SQLite)

### What's Fast

Operations that delegate to native Go implementations:
- **File I/O** - Direct Go file operations
- **HTTP requests** - Go's `net/http` client
- **JSON parsing** - Go's `encoding/json`
- **SQLite queries** - Native SQLite via CGO
- **Regex matching** - Go's `regexp` package
- **Compression** - Go's `compress/*` packages
- **Cryptography** - Go's `crypto/*` packages

### What's Slow

Operations that execute in the interpreter:
- **Recursive function calls** - AST traversal overhead
- **Tight loops** - No JIT compilation
- **Complex arithmetic** - Interpreted operations
- **String manipulation** - Frequent allocations

## Benchmarking BASL

To measure actual performance, create benchmark scripts:

### Fibonacci Benchmark

**benchmark_fib.basl:**
```c
import "time";
import "fmt";

fn fib(i32 n) -> i32 {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

fn main() -> i32 {
    i64 start = time.now();
    i32 result = fib(35);
    i64 elapsed = time.since(start);
    
    fmt.println(f"fib(35) = {result}");
    fmt.println(f"Time: {elapsed}ms");
    return 0;
}
```

Run: `basl benchmark_fib.basl`

### Array Operations Benchmark

**benchmark_array.basl:**
```c
import "time";
import "fmt";

fn main() -> i32 {
    i64 start = time.now();
    
    array<i32> nums = [];
    for (i32 i = 0; i < 1000000; i++) {
        nums.push(i);
    }
    
    i32 sum = 0;
    for val in nums {
        sum += val;
    }
    
    i64 elapsed = time.since(start);
    fmt.println(f"Sum: {sum}");
    fmt.println(f"Time: {elapsed}ms");
    return 0;
}
```

### HTTP Server Benchmark

Use standard tools like `wrk` or `ab`:

```bash
# Start BASL HTTP server
basl examples/http_server.basl &

# Benchmark with wrk
wrk -t4 -c100 -d10s http://localhost:8080/
```

## Optimization Tips

1. **Use native stdlib functions** - They run at Go speed
   ```c
   // Slow: manual string building
   string result = "";
   for (i32 i = 0; i < 1000; i++) {
       result += "x";
   }
   
   // Fast: use stdlib
   string result = strings.repeat("x", 1000);
   ```

2. **Minimize function call depth** - Reduce recursion when possible
   ```c
   // Slower: deep recursion
   fn sum_recursive(array<i32> arr, i32 idx) -> i32 {
       if (idx >= arr.len()) { return 0; }
       return arr[idx] + sum_recursive(arr, idx + 1);
   }
   
   // Faster: iterative
   fn sum_iterative(array<i32> arr) -> i32 {
       i32 total = 0;
       for val in arr { total += val; }
       return total;
   }
   ```

3. **Batch I/O operations** - Reduce interpreter overhead
   ```c
   // Slower: many small writes
   for line in lines {
       file.write_all("out.txt", line + "\n");
   }
   
   // Faster: single write
   string content = strings.join(lines, "\n");
   file.write_all("out.txt", content);
   ```

4. **Use appropriate data structures**
   - Maps for lookups: O(1) average case
   - Arrays for sequential access
   - Avoid nested loops when possible

## Memory Usage

BASL's memory footprint:
- **Base interpreter**: ~10-15 MB (Go runtime + BASL VM)
- **Per-script overhead**: ~1-5 MB (AST + symbol tables)
- **Packaged executable**: ~15-20 MB (includes interpreter)

## Startup Time

BASL starts quickly due to:
- Simple runtime initialization
- No bytecode compilation step
- Direct AST execution

Expected startup: **< 10ms** for typical scripts

## Concurrency Performance

BASL threads map to OS threads (via Go goroutines):
- Thread creation uses Go's goroutine mechanism
- Context switching managed by Go scheduler
- Mutex operations use native Go sync primitives

**Note:** BASL has a Global Interpreter Lock (GIL) for stdlib operations, similar to Python. CPU-bound parallel work won't scale linearly.

## When to Use BASL

### Good Use Cases ✅

- **Build scripts and automation** - Fast enough, easier than shell scripts
- **Data processing pipelines** - I/O bound, benefits from native stdlib
- **CLI tools** - Startup time is acceptable, development is fast
- **Configuration and glue code** - Minimal computation, mostly orchestration
- **Prototyping** - Rapid iteration, easy to refactor
- **Embedded scripting** - Extend Go apps with user-defined logic

### Poor Use Cases ❌

- **High-frequency trading** - Need microsecond latency
- **Game engines** - Need 60+ FPS with complex logic
- **Video encoding** - CPU-intensive, needs SIMD
- **Machine learning training** - Needs GPU acceleration
- **Real-time audio processing** - Needs deterministic timing

## Comparison Summary

BASL sits between compiled languages (Go) and dynamic languages (Python):

| Aspect | BASL Position |
|--------|---------------|
| **Execution Speed** | Interpreted, similar to Python |
| **Startup Time** | Fast, < 10ms typical |
| **Memory Usage** | Moderate, ~15-20 MB base |
| **Development Speed** | Fast, with static typing benefits |
| **Type Safety** | Static types with runtime enforcement |
| **Deployment** | Single binary, easy distribution |

**General guidance:**
- For maximum performance → Use Go
- For maximum ecosystem → Use Python  
- For scripting with type safety → Use BASL

## Profiling Your Code

Potential performance improvements:
- **Bytecode compilation** - Reduce AST traversal overhead
- **JIT compilation** - Compile hot paths to native code
- **Constant folding** - Optimize compile-time expressions
- **Inline caching** - Speed up method dispatch
- **Escape analysis** - Reduce allocations

These are not currently implemented but could improve performance 2-10x for compute-heavy workloads.

## Profiling Your Code

Use Go's profiling tools on the BASL interpreter:

```bash
# CPU profiling
go test ./pkg/basl/interp -bench=. -cpuprofile=cpu.prof
go tool pprof -top cpu.prof

# Memory profiling
go test ./pkg/basl/interp -bench=. -memprofile=mem.prof
go tool pprof -top -alloc_space mem.prof
```

## Conclusion

BASL trades raw performance for:
- **Simplicity** - Easy to learn and use
- **Safety** - Static typing with runtime checks
- **Productivity** - Fast development iteration
- **Portability** - Single binary deployment

If you need maximum performance, use Go. If you need maximum flexibility and ecosystem, use Python. If you want a balance of safety, simplicity, and reasonable performance for scripting tasks, use BASL.
