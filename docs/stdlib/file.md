# file

Filesystem operations: read, write, directory management, traversal, and file handles.

```
import "file";
```

## Error Messages

Filesystem errors are translated to user-friendly messages:
- File not found → `"file not found: PATH"`
- Permission denied → `"permission denied: PATH"`
- File already exists → `"file already exists: PATH"`
- Other → `"file error: PATH"`

## Convenience Functions

### file.read_all(string path) -> (string, err)

Reads the entire file contents as a string.

- Returns `(contents, ok)` on success.
- Returns `("", err(message, err.io))` on failure.

```c
string data, err e = file.read_all("config.txt");
```

### file.write_all(string path, string data) -> err

Writes `data` to the file, creating it if it doesn't exist, truncating if it does. File permissions: `0644`.

- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = file.write_all("out.txt", "hello");
```

### file.append(string path, string data) -> err

Appends `data` to the file, creating it if it doesn't exist. File permissions: `0644`.

```c
err e = file.append("log.txt", "new line\n");
```

### file.read_lines(string path) -> (array\<string\>, err)

Reads the file and splits into lines. Trims trailing newline before splitting.

- Returns `(lines, ok)` on success.
- Returns `([], err(message, err.io))` on failure.

```c
array<string> lines, err e = file.read_lines("data.txt");
```

### file.remove(string path) -> err

Removes a single file or empty directory.

```c
err e = file.remove("temp.txt");
```

### file.rename(string old, string new) -> err

Renames (moves) a file.

```c
err e = file.rename("old.txt", "new.txt");
```

### file.mkdir(string path) -> err

Creates a directory and all necessary parents (like `mkdir -p`). Directory permissions: `0755`.

```c
err e = file.mkdir("a/b/c");
```

### file.touch(string path) -> err

Creates an empty file or updates the modification time of an existing file.

- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.
- If file doesn't exist, creates it with permissions `0644`.
- If file exists, updates access and modification times to current time.

```c
err e = file.touch("newfile.txt");
```

### file.list_dir(string path) -> (array\<string\>, err)

Lists the names of entries in a directory.

- Returns `(names, ok)` on success.
- Returns `([], err(message, err.io))` on failure.

```c
array<string> entries, err e = file.list_dir(".");
```

### file.read_dir(string path) -> (array\<string\>, err)

Alias for `file.list_dir(path)`.

### file.walk(string root) -> (array\<file.Entry\>, err)

Recursively walks `root` in lexicographic order (fail-fast mode).

- Does **not** follow symlinked directories.
- Includes `root` itself as the first entry.
- Returns `(entries, ok)` on success.
- Returns `([], err(...))` on the first traversal error.

```c
array<file.Entry> entries, err e = file.walk("src");
```

### file.walk_follow_links(string root) -> (array\<file.Entry\>, err)

Recursively walks `root` and follows symlinked directories (fail-fast mode).

- Includes `root` itself as the first entry.
- Detects symlink cycles and returns `err(..., err.state)` instead of recursing forever.
- Returns `([], err(...))` on the first traversal error.

```c
array<file.Entry> entries, err e = file.walk_follow_links("src");
```

### file.walk_best_effort(string root) -> (array\<file.Entry\>, array\<file.WalkIssue\>)

Recursively walks `root` in lexicographic order (best-effort mode).

- Does **not** follow symlinked directories.
- Includes `root` itself as the first entry when readable.
- Continues walking after per-path errors.
- Returns collected entries plus per-path issues.

```c
array<file.Entry> entries, array<file.WalkIssue> issues = file.walk_best_effort("src");
```

### file.walk_follow_links_best_effort(string root) -> (array\<file.Entry\>, array\<file.WalkIssue\>)

Best-effort traversal that follows symlinked directories.

- Detects symlink cycles and reports them in `issues` as `err.state`.
- Continues walking other paths after recoverable failures.

```c
array<file.Entry> entries, array<file.WalkIssue> issues = file.walk_follow_links_best_effort("src");
```

### file.exists(string path) -> bool

Returns `true` if the path exists, `false` otherwise. Does not return an error.

```c
bool found = file.exists("config.json");
```

## File Handles

### file.open(string path, string mode) -> (File, err)

Opens a file handle for streaming I/O.

Modes:
| Mode | Behavior |
|------|----------|
| `"r"` | Read-only |
| `"w"` | Write-only, create/truncate |
| `"a"` | Write-only, create/append |
| `"rw"` | Read-write, create |

- Returns `(File, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.
- Invalid mode returns `err("file.open: invalid mode: MODE", err.arg)`.

```c
File f, err e = file.open("data.txt", "r");
```

### file.stat(string path) -> (FileStat, err)

Returns file metadata.

- Returns `(FileStat, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

FileStat fields:
| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Base filename |
| `size` | `i32` | File size in bytes |
| `is_dir` | `bool` | Whether it's a directory |
| `mod_time` | `string` | Last modified time (ISO 8601: `"2006-01-02T15:04:05Z07:00"`) |
| `mode` | `i32` | Full file mode bits |

```c
FileStat info, err e = file.stat("data.txt");
fmt.println(info.name);      // "data.txt"
fmt.println(info.size);      // 1234
fmt.println(info.is_dir);    // false
fmt.println(info.mod_time);  // "2024-01-15T10:30:00-05:00"
```

### file.Entry

Object returned by `file.walk*` functions.

Entry fields:
| Field | Type | Description |
|-------|------|-------------|
| `path` | `string` | Full walked path |
| `name` | `string` | Base name |
| `is_dir` | `bool` | Whether the entry is a directory |
| `size` | `i32` | Size in bytes |
| `mode` | `i32` | Full file mode bits |
| `mod_time` | `string` | Last modified time (ISO 8601) |

### file.WalkIssue

Object returned in best-effort walk modes.

Issue fields:
| Field | Type | Description |
|-------|------|-------------|
| `path` | `string` | Path that failed |
| `err` | `err` | Error value for the path |

## File Methods

### File.write(string data) -> err

Writes a string to the file handle.

### File.read(i32 count) -> (string, err)

Reads up to `count` bytes. Returns the data read (may be less than `count` at EOF).

### File.read_line() -> (string, err)

Reads one line (up to `\n`). The newline character is not included in the result.

- At end of file with no remaining data: returns `("", err("EOF", err.eof))`.
- At end of file with a partial line (no trailing newline): returns the partial line with `ok`.
- Reads byte-by-byte internally.

```c
File f, err e = file.open("data.txt", "r");
while (true) {
    string line, err re = f.read_line();
    if (string(re) != "ok") { break; }
    fmt.println(line);
}
f.close();
```

### File.close() -> err

Closes the file handle. Returns `err(message, err.io)` if the close fails.
