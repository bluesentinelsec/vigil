# BASL `bundle` CLI

`basl bundle` packages a BASL library project into a distributable directory that can be easily dropped into other projects.

## Quick Start

Bundle a library:

```sh
cd mylib
basl bundle
```

This creates `mylib-bundle/` containing:
- `lib/` - All library source files
- `basl.toml` - Project metadata
- `README.txt` - Usage instructions

## Usage

```sh
basl bundle [-o output-dir] [project-dir]
```

### Options

- `-o output-dir` - Specify output directory (default: `<project-name>-bundle`)
- `project-dir` - Library project to bundle (default: current directory)

### Examples

```sh
# Bundle current library project
basl bundle

# Bundle specific project
basl bundle ./mylib

# Custom output directory
basl bundle -o dist/mylib-v1.0 ./mylib
```

## What Gets Bundled

The bundle includes:
- **lib/** - All `.basl` source files from the library
- **basl.toml** - Project manifest with name, version, dependencies
- **README.txt** - Auto-generated usage instructions

The bundle does NOT include:
- `test/` directory (tests are for development only)
- `deps/` directory (users manage their own dependencies)
- `main.basl` (libraries shouldn't have entry points)

## Using a Bundled Library

To use a bundled library in your project:

1. Copy the bundle's `lib/` directory to your project's `deps/`:
   ```sh
   cp -r mylib-bundle/lib/ myproject/deps/mylib/
   ```

2. Import modules from the library:
   ```c
   import "mylib/utils";
   import "mylib/http/client";
   ```

## Bundle vs Package

| Feature | `basl bundle` | `basl package` |
|---------|---------------|----------------|
| Purpose | Distribute libraries | Distribute applications |
| Output | Directory with source | Single executable binary |
| Contains | `.basl` source files | Compiled interpreter + source |
| Use case | Reusable code | Standalone programs |

Use `basl bundle` for libraries that other projects will import.
Use `basl package` for applications that users will run.

## Example Workflow

### Creating a library

```sh
# Create library project
basl new mylib --lib

# Write library code in lib/
# Write tests in test/

# Test the library
basl test

# Bundle for distribution
basl bundle -o dist/mylib-1.0.0
```

### Using a library

```sh
# Download or receive mylib-1.0.0 bundle

# Copy to your project
cp -r mylib-1.0.0/lib/ myproject/deps/mylib/

# Use in your code
# myproject/main.basl:
import "mylib/utils";

fn main() -> i32 {
    mylib.hello();
    return 0;
}
```

## Distribution

Bundled libraries can be distributed via:
- Git repositories (commit the bundle directory)
- Zip/tar archives
- Package registries (future)
- Direct file sharing

The bundle is just a directory of source files, so it's easy to version control and distribute.

## Best Practices

1. **Version your bundles** - Include version in output directory name:
   ```sh
   basl bundle -o mylib-1.2.3
   ```

2. **Document your API** - Include comments in your library code that `basl doc` can display

3. **Test before bundling** - Always run `basl test` before creating a bundle

4. **Keep dependencies minimal** - Libraries with fewer dependencies are easier to use

5. **Follow naming conventions** - Use clear module names that won't conflict with other libraries
