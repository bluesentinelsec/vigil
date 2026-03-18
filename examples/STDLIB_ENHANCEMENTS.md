# Stdlib Enhancement PRs for Unix Tools 2

This document tracks the stdlib function additions required for implementing Unix tools 2 (ls, cp, mv, rm, mkdir, rmdir, touch, ln, chmod).

## Created PRs (All based on origin/v0.1.1)

### PR 1: add-file-copy
**Branch:** `add-file-copy`  
**Status:** Ready for review  
**Function:** `file.copy(string src, string dst) -> err`

**Purpose:** Efficient file copying for `cp` command

**Implementation:**
- Uses `io.Copy` for streaming (memory efficient)
- Overwrites destination if exists
- Cross-platform compatible

**Tests:**
- Basic copy operation
- Error handling for missing source
- Destination overwrite behavior

---

### PR 2: add-file-links
**Branch:** `add-file-links`  
**Status:** Ready for review  
**Functions:**
- `file.symlink(string target, string link) -> err`
- `file.link(string target, string link) -> err`
- `file.readlink(string path) -> (string, err)`

**Purpose:** Symbolic and hard link operations for `ln` command

**Implementation:**
- Uses `os.Symlink`, `os.Link`, `os.Readlink`
- Symlinks require admin on Windows
- Hard links work on all platforms

**Tests:**
- Create and read through symlink
- Read symlink target
- Create hard link and verify shared content
- Error handling for non-symlinks

---

### PR 3: add-file-chmod
**Branch:** `add-file-chmod`  
**Status:** Ready for review  
**Function:** `file.chmod(string path, i32 mode) -> err`  
**Enhancement:** Added `mode` field to `FileStat`

**Purpose:** Permission management for `chmod` command

**Implementation:**
- Uses `os.Chmod` for permission changes
- `FileStat.mode` contains full `os.FileMode` value
- Lower 9 bits (& 0o777) contain Unix permissions
- Windows has limited support (read-only flag only)

**Common modes:**
- `0o644` (420) - rw-r--r--
- `0o755` (493) - rwxr-xr-x
- `0o600` (384) - rw-------
- `0o777` (511) - rwxrwxrwx

**Tests:**
- Change permissions to 0600
- Verify mode field exists
- Make file executable (0755)
- Error handling

---

### PR 4: add-file-touch
**Branch:** `add-file-touch`  
**Status:** Ready for review  
**Function:** `file.touch(string path) -> err`

**Purpose:** Create empty files and update timestamps for `touch` command

**Implementation:**
- If file doesn't exist: creates with 0644 permissions
- If file exists: updates access and modification times
- Uses `os.Chtimes` for timestamp updates
- Cross-platform compatible

**Tests:**
- Create new empty file
- Update existing file, preserve content
- Verify timestamp actually changes

---

## Implementation Status

### Can Now Implement (After PRs Merge)
✅ **All 9 tools** can be fully implemented:
1. **ls** - List directory (file.list_dir + file.stat with mode)
2. **cp** - Copy files (file.copy)
3. **mv** - Move/rename (file.rename - already exists)
4. **rm** - Remove files (file.remove - already exists)
5. **mkdir** - Create directories (file.mkdir - already exists)
6. **rmdir** - Remove empty directories (file.remove - already exists)
7. **touch** - Create/update files (file.touch)
8. **ln** - Create links (file.symlink, file.link)
9. **chmod** - Change permissions (file.chmod)

### Remaining Limitations
- **Recursive operations** still need manual implementation in VIGIL
  - `cp -r` (recursive copy)
  - `rm -r` (recursive remove)
  - `ls -R` (recursive listing)
- **Extended file metadata** not available
  - Owner/group (uid/gid)
  - Link count
  - File type detection beyond is_dir

These limitations are acceptable for initial implementation and can be addressed in future enhancements if needed.

---

## Next Steps

1. **Wait for PR reviews and merges**
2. **Implement Unix tools 2** on `unix-tools-2` branch
3. **Add tests** for each tool
4. **Document** in UNIX_UTILS.md
5. **Submit** unix-tools-2 PR

---

## Cross-Platform Notes

All added functions are cross-platform compatible with documented limitations:

- **Windows symlinks:** Require admin privileges or Developer Mode
- **Windows chmod:** Limited to read-only flag (not full Unix permissions)
- **All other functions:** Full support on Windows, macOS, Linux

Tests skip platform-specific features on unsupported platforms.
