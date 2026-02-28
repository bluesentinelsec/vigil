# os

Operating system interaction: environment, process control, command execution.

```
import "os";
```

## Functions

### os.args() -> array\<string\>

Returns the command-line arguments passed to the script.

```c
array<string> a = os.args();
```

### os.env(string key) -> (string, bool)

Looks up an environment variable.

- Returns `(value, true)` if the variable is set.
- Returns `("", false)` if the variable is not set.
- Error if arg is not a string: `"os.env: expected string key"`.

```c
string val, bool found = os.env("HOME");
if (found) { fmt.println(val); }
```

### os.set_env(string key, string value) -> err

Sets an environment variable for the current process.

- Returns `ok` on success.
- Returns `err(message)` if the OS rejects the call.
- Error if args are not two strings: `"os.set_env: expected (string key, string value)"`.

```c
os.set_env("MY_VAR", "hello");
```

### os.exit(i32 code)

Terminates the process immediately with the given exit code.

- If no argument or argument is not `i32`, exits with code 0.
- Does not return.

```c
os.exit(1);
```

### os.cwd() -> (string, err)

Returns the current working directory.

- Returns `(path, ok)` on success.
- Returns `("", err(message))` on failure.

```c
string dir, err e = os.cwd();
```

### os.hostname() -> (string, err)

Returns the system hostname.

- Returns `(hostname, ok)` on success.
- Returns `("", err(message))` on failure.

```c
string host, err e = os.hostname();
```

### os.platform() -> string

Returns the operating system name as a string. Values match Go's `runtime.GOOS`: `"darwin"`, `"linux"`, `"windows"`, etc.

```c
string p = os.platform();  // "darwin", "linux", "windows"
```

### os.exec(string cmd, ...string args) -> (string, string, err)

Executes an external command and captures its output.

- Returns `(stdout, stderr, ok)` on success (exit code 0).
- Returns `(stdout, stderr, err(message))` on non-zero exit.
- stdout and stderr are always populated regardless of success/failure.
- Error if first arg is not a string: `"os.exec: expected (string cmd, ...string args)"`.
- Error if any subsequent arg is not a string: `"os.exec: arg N must be string"`.

```c
string out, string errs, err e = os.exec("echo", "hello");
// out = "hello\n", errs = "", e = ok
```
