# archive

Create and extract tar and zip archives.

```
import "archive";
```

## Functions

### archive.tar_create(string path, array\<string\> files) -> err

Creates a tar archive at `path` containing the listed files.

- Each file is added with its base name (not the full path).
- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = archive.tar_create("backup.tar", ["file1.txt", "file2.txt"]);
```

### archive.tar_extract(string path, string dest_dir) -> err

Extracts a tar archive into `dest_dir`. Creates the directory if needed.

- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = archive.tar_extract("backup.tar", "./extracted");
```

### archive.zip_create(string path, array\<string\> files) -> err

Creates a zip archive at `path` containing the listed files.

- Each file is added with its base name.
- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = archive.zip_create("backup.zip", ["file1.txt", "file2.txt"]);
```

### archive.zip_extract(string path, string dest_dir) -> err

Extracts a zip archive into `dest_dir`. Creates the directory if needed.

- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = archive.zip_extract("backup.zip", "./extracted");
```
