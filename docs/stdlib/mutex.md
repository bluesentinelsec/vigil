# mutex

Mutual exclusion locks via pthreads. Used with the `thread` module for thread-safe shared state.

```
import "mutex";
```

## Functions

### mutex.new() -> (Mutex, err)

Creates a new mutex (heap-allocated via `malloc`).

- Returns `(mutex, ok)` on success.
- Returns `(void, err("mutex init failed: N"))` on failure.

```c
Mutex m, err e = mutex.new();
```

## Mutex Methods

### m.lock() -> err

Acquires the mutex. Blocks if already held by another thread.

- Returns `ok` on success.
- Returns `err("mutex lock failed: N")` on failure.

### m.unlock() -> err

Releases the mutex.

- Returns `ok` on success.
- Returns `err("mutex unlock failed: N")` on failure.

### m.destroy() -> err

Destroys the mutex and frees its memory. Must not be called while the mutex is locked.

- Returns `ok` on success.
- Returns `err("mutex destroy failed: N")` on failure.

```c
Mutex m, err e = mutex.new();
m.lock();
// ... critical section ...
m.unlock();
m.destroy();
```

## Example: Thread-Safe Counter

```c
import "thread";
import "mutex";
import "fmt";

i32 counter = 0;

fn increment(Mutex m) {
    i32 i = 0;
    while (i < 1000) {
        m.lock();
        counter = counter + 1;
        m.unlock();
        i = i + 1;
    }
}

fn main() -> i32 {
    Mutex m, err e = mutex.new();
    Thread t1, err e1 = thread.spawn(increment, m);
    Thread t2, err e2 = thread.spawn(increment, m);
    t1.join();
    t2.join();
    m.destroy();
    fmt.println(string(counter));  // 2000
    return 0;
}
```
