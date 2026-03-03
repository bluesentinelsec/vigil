# time

Time operations using millisecond-precision Unix timestamps.

```
import "time";
```

## Functions

### time.now() -> i64

Returns the current time as Unix epoch milliseconds (`i64`).

```c
i64 ts = time.now();  // e.g., 1706000000000
```

### time.sleep(i32 ms)

Blocks the current thread for `ms` milliseconds.

- Error if arg is not `i32`: `"time.sleep: expected i32 milliseconds"`.

```c
time.sleep(1000);  // sleep 1 second
```

### time.since(i64 epoch_millis) -> i64

Returns the number of milliseconds elapsed since the given timestamp.

- Error if arg is not `i64`: `"time.since: expected i64 epoch_millis"`.

```c
i64 start = time.now();
// ... work ...
i64 elapsed = time.since(start);  // milliseconds elapsed
```

### time.format(i64 epoch_millis, string layout) -> string

Formats a timestamp using a Go reference-time layout string. The timestamp is interpreted in the local timezone.

Go reference time: `Mon Jan 2 15:04:05 MST 2006`

- Error if args are not `(i64, string)`: `"time.format: expected (i64 epoch_millis, string layout)"`.

```c
string s = time.format(time.now(), "2006-01-02 15:04:05");
// e.g., "2024-01-15 10:30:00"
```

### time.parse(string layout, string value) -> (i64, err)

Parses a time string using a Go reference-time layout and returns epoch milliseconds.

- Returns `(millis, ok)` on success.
- Returns `(0, err(message, err.parse))` on parse failure.

```c
i64 ms, err e = time.parse("2006-01-02", "2024-01-15");
```
