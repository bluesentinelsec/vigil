# Unix Tools 2 - Implementation Assessment

## Target Tools

1. **ls** - List directory contents
2. **cp** - Copy files and directories
3. **mv** - Move/rename files and directories
4. **rm** - Remove files and directories
5. **mkdir** - Create directories
6. **rmdir** - Remove empty directories
7. **touch** - Create empty files or update timestamps
8. **ln** - Create symbolic/hard links
9. **chmod** - Change file permissions

## Cross-Platform Requirements

All implementations must work on:
- Linux
- macOS
- Windows

## Current Stdlib Capabilities

### Available (file module)
- ✅ `file.read_all()` - Read file contents
- ✅ `file.write_all()` - Write file contents
- ✅ `file.append()` - Append to file
- ✅ `file.remove()` - Remove file or empty directory
- ✅ `file.rename()` - Rename/move file
- ✅ `file.mkdir()` - Create directory with parents
- ✅ `file.list_dir()` - List directory entries
- ✅ `file.exists()` - Check if path exists
- ✅ `file.stat()` - Get file metadata (name, size, is_dir, mod_time)
- ✅ `file.open()` - Open file handle for streaming

### Available (path module)
- ✅ `path.join()` - Join path segments
- ✅ `path.dir()` - Get directory portion
- ✅ `path.base()` - Get filename
- ✅ `path.ext()` - Get file extension
- ✅ `path.abs()` - Get absolute path

### Available (os module)
- ✅ `os.args()` - Command-line arguments
- ✅ `os.platform()` - OS detection
- ✅ `os.cwd()` - Current working directory

## Identified Gaps

### CRITICAL: Missing Functionality

1. **❌ Recursive directory operations**
   - **Impact:** HIGH
   - **Needed for:** `cp -r`, `rm -r`, `ls -R`
   - **Current:** `file.list_dir()` only lists immediate children
   - **Required:** Need recursive directory traversal
   - **Workaround:** Implement recursion in BASL code

2. **❌ Copy file operation**
   - **Impact:** HIGH
   - **Needed for:** `cp` command
   - **Current:** No `file.copy()` function
   - **Workaround:** Read entire file + write (inefficient for large files)
   - **Better:** Need streaming copy or native `file.copy()`

3. **❌ Symbolic/hard link operations**
   - **Impact:** HIGH
   - **Needed for:** `ln` command
   - **Current:** No link creation functions
   - **Required:** `file.symlink()`, `file.link()`, `file.readlink()`
   - **Cross-platform:** Windows symlinks require admin privileges

4. **❌ File permissions/mode operations**
   - **Impact:** HIGH
   - **Needed for:** `chmod` command
   - **Current:** `file.stat()` doesn't expose permissions
   - **Required:** `file.chmod()`, `file.stat()` should return mode
   - **Cross-platform:** Windows has different permission model

5. **❌ Touch/timestamp operations**
   - **Impact:** MEDIUM
   - **Needed for:** `touch` command
   - **Current:** No way to update file timestamps
   - **Required:** `file.touch()` or `file.set_times()`
   - **Workaround:** Create empty file with `file.write_all("", "")`

6. **❌ Directory-specific removal**
   - **Impact:** LOW
   - **Needed for:** `rmdir` (remove only empty dirs)
   - **Current:** `file.remove()` removes files or empty dirs
   - **Workaround:** Check `file.stat().is_dir` before removal

### MEDIUM: Limited Functionality

7. **⚠️ File metadata incomplete**
   - **Impact:** MEDIUM
   - **Needed for:** `ls -l` (detailed listing)
   - **Current:** `FileStat` has: name, size, is_dir, mod_time
   - **Missing:** permissions, owner, group, link count
   - **Cross-platform:** Windows doesn't have Unix permissions

8. **⚠️ No file type detection**
   - **Impact:** LOW
   - **Needed for:** `ls` color coding, type indicators
   - **Current:** Only `is_dir` boolean
   - **Missing:** symlink, socket, pipe, device detection
   - **Workaround:** Use `is_dir` only

## Implementation Strategy

### Phase 1: Implement with Current Stdlib (Workarounds)

These can be implemented NOW with existing stdlib:

1. **mkdir** ✅ - Use `file.mkdir()` directly
2. **rmdir** ✅ - Use `file.remove()` with `is_dir` check
3. **touch** ✅ - Use `file.write_all()` for creation, skip timestamp update
4. **mv** ✅ - Use `file.rename()` directly
5. **rm** ⚠️ - Basic file removal works, recursive needs manual implementation
6. **ls** ⚠️ - Basic listing works, detailed mode limited by metadata
7. **cp** ⚠️ - Single file copy via read/write, recursive needs manual implementation

### Phase 2: Cannot Implement (Missing Stdlib)

These REQUIRE new stdlib functions:

8. **ln** ❌ - Blocked on `file.symlink()`, `file.link()`
9. **chmod** ❌ - Blocked on `file.chmod()`, permission metadata

## Recommendations

### Option A: Implement Subset Now
Implement 7 tools (mkdir, rmdir, touch, mv, rm, ls, cp) with limitations:
- No recursive operations (or implement in BASL)
- No detailed file metadata in ls
- No timestamp updates in touch
- Document limitations

### Option B: Add Stdlib Functions First
Add missing stdlib functions, then implement all 9 tools properly:
- `file.copy(src, dst)` - Efficient file copy
- `file.symlink(target, link)` - Create symbolic link
- `file.link(target, link)` - Create hard link
- `file.readlink(path)` - Read symlink target
- `file.chmod(path, mode)` - Change permissions
- `file.touch(path)` - Create or update timestamps
- Extend `FileStat` with: mode, uid, gid, nlink

### Option C: Hybrid Approach (RECOMMENDED)
1. Implement basic versions of 7 tools NOW (mkdir, rmdir, touch, mv, rm, ls, cp)
2. Document limitations clearly
3. Create separate PR for stdlib additions
4. Enhance tools once stdlib is extended
5. Defer `ln` and `chmod` until stdlib support exists

## Cross-Platform Considerations

### Windows Compatibility Issues

1. **Path separators:** Use `path.join()` everywhere (handles OS differences)
2. **Permissions:** Windows uses ACLs, not Unix modes
   - `chmod` may need to be Unix-only or simplified for Windows
3. **Symlinks:** Require admin privileges on Windows
   - `ln -s` may fail without elevation
4. **Case sensitivity:** Windows is case-insensitive by default
5. **Hidden files:** Windows uses attributes, Unix uses `.` prefix

### Portable Implementation Guidelines

- Use `os.platform()` to detect OS when needed
- Use `path.join()` for all path construction
- Test on all three platforms
- Document platform-specific limitations
- Gracefully handle unsupported operations

## Conclusion

**Recommended approach:** Option C (Hybrid)

Implement 7 tools immediately with current stdlib:
- ✅ mkdir, rmdir, touch, mv (full functionality)
- ⚠️ rm, ls, cp (basic functionality, document limitations)

Defer until stdlib extended:
- ❌ ln (needs symlink/hardlink functions)
- ❌ chmod (needs permission functions)

This validates the language further while identifying concrete stdlib gaps for future enhancement.
