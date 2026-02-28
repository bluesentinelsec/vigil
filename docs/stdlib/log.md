# log

Leveled logging with configurable output handler.

```
import "log";
```

## Log Levels

| Level | Value | Function |
|-------|-------|----------|
| debug | 0 | `log.debug(msg)` |
| info | 1 | `log.info(msg)` |
| warn | 2 | `log.warn(msg)` |
| error | 3 | `log.error(msg)` |
| fatal | 4 | `log.fatal(msg)` |

Default level is `info` (1). Messages below the current level are silently discarded.

## Functions

### log.debug(val) / log.info(val) / log.warn(val) / log.error(val) / log.fatal(val)

Logs a message at the given level. Each accepts exactly 1 argument.

- If the message's level is below the current log level, the call is a no-op.
- Output priority: BASL handler (if set) → Go LogFn (if set) → stderr.
- Default stderr format: `[LEVEL] message\n` (e.g., `[INFO] starting up`).
- `log.fatal` calls `os.Exit(1)` after logging. It does not return.
- Error if argument count ≠ 1: `"log.NAME: expected 1 argument"`.

```c
log.info("starting up");    // [INFO] starting up
log.error("bad input");     // [ERROR] bad input
log.fatal("cannot continue"); // [FATAL] cannot continue → exit(1)
```

### log.set_level(string level)

Sets the minimum log level. Messages below this level are discarded.

- Valid values: `"debug"`, `"info"`, `"warn"`, `"error"`, `"fatal"`.
- Error if unknown level: `"log.set_level: unknown level \"NAME\""`.

```c
log.set_level("debug");  // show all messages
log.set_level("error");  // only error and fatal
```

### log.set_handler(fn handler)

Sets a custom BASL function as the log handler. The function receives `(string level, string message)`.

- The level tag is uppercase: `"DEBUG"`, `"INFO"`, `"WARN"`, `"ERROR"`, `"FATAL"`.
- Error if arg is not a function: `"log.set_handler: expected fn(string level, string msg)"`.

```c
fn my_handler(string level, string msg) {
    fmt.println(fmt.sprintf("[%s] %s", level, msg));
}
log.set_handler(my_handler);
log.info("test");  // calls my_handler("INFO", "test")
```
