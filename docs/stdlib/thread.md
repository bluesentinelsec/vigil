# thread

OS-level threading via pthreads. Spawns real OS threads (not goroutines).

```
import "thread";
```

## Global Interpreter Lock (GIL)

BASL uses a GIL to make all interpreter state thread-safe. Only one thread executes BASL code at a time. The GIL is released during blocking operations (`thread.sleep`, `Thread.join`) so other threads can run.

This means:
- All stdlib functions are safe to call from any thread.
- Shared objects (classes, arrays, maps) are safe to read/write from threads without explicit locking.
- CPU-bound threads do not run in parallel — they take turns.
- I/O-bound threads (sleeping, joining, network) overlap effectively because the GIL is released while waiting.

The `mutex` module is still useful for application-level invariants (e.g., read-modify-write sequences that must be atomic from the program's perspective).

## Functions

### thread.spawn(fn func, ...args) -> (Thread, err)

Spawns a new OS thread that executes `func` with the given arguments.

- The function and arguments are snapshot-copied for the new thread.
- The spawned thread acquires the GIL before executing BASL code.
- Returns `(thread, ok)` on success.
- Returns `(void, err(message))` on failure (e.g., callback slot exhaustion, pthread_create failure).

```c
fn worker(i32 x) -> i32 {
    return x * 2;
}
Thread th, err e = thread.spawn(worker, 21);
```

### thread.sleep(i32 ms)

Sleeps the current thread for `ms` milliseconds. Uses `nanosleep` (not Go's `time.Sleep`).

- Releases the GIL while sleeping, allowing other threads to run.
- Error if arg is not `i32`: `"thread.sleep: expected i32 milliseconds"`.

```c
thread.sleep(100);  // sleep 100ms
```

## Thread Methods

### th.join() -> (value, err)

Blocks until the thread completes and returns its result.

- Releases the GIL while waiting, allowing the joined thread to run.
- Returns `(result, ok)` on success, where `result` is the return value of the spawned function.
- Returns `(void, err(message))` on failure (pthread_join failure, or if the spawned function errored).
- Joining a thread that was already joined returns `err("thread already joined", err.state)`.
- Frees the callback slot after joining.

```c
Thread th, err e1 = thread.spawn(worker, 21);
i32 result, err e2 = th.join();
// result = 42
```
