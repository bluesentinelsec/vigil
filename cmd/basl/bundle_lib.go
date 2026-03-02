package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

func runLibraryBundle(projectDir, outputDir string) int {
	if projectDir == "" {
		projectDir = "."
	}

	// Check if it's a library project
	manifestPath := filepath.Join(projectDir, "basl.toml")
	if _, err := os.Stat(manifestPath); err != nil {
		fmt.Fprintf(os.Stderr, "error: %s is not a BASL project (no basl.toml)\n", projectDir)
		return 1
	}

	// Determine output directory
	if outputDir == "" {
		baseName := filepath.Base(projectDir)
		if baseName == "." {
			cwd, _ := os.Getwd()
			baseName = filepath.Base(cwd)
		}
		outputDir = baseName + "-bundle"
	}

	// Create output directory
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "error: failed to create output directory: %v\n", err)
		return 1
	}

	// Copy lib/ directory
	libSrc := filepath.Join(projectDir, "lib")
	libDst := filepath.Join(outputDir, "lib")
	if err := copyDir(libSrc, libDst); err != nil {
		fmt.Fprintf(os.Stderr, "error: failed to copy lib/: %v\n", err)
		return 1
	}

	// Copy basl.toml
	manifestDst := filepath.Join(outputDir, "basl.toml")
	if err := copyFile(manifestPath, manifestDst); err != nil {
		fmt.Fprintf(os.Stderr, "error: failed to copy basl.toml: %v\n", err)
		return 1
	}

	// Create README with usage instructions
	readmePath := filepath.Join(outputDir, "README.txt")
	readme := fmt.Sprintf(`BASL Library Bundle

To use this library in your project:

1. Copy the lib/ directory to your project's deps/ directory:
   cp -r lib/ /path/to/your/project/deps/%s/

2. Import modules from this library:
   import "%s/modulename";

See basl.toml for library metadata.
`, filepath.Base(outputDir), filepath.Base(outputDir))

	if err := os.WriteFile(readmePath, []byte(readme), 0644); err != nil {
		fmt.Fprintf(os.Stderr, "warning: failed to create README: %v\n", err)
	}

	fmt.Printf("Library bundled to: %s\n", outputDir)
	return 0
}

func copyDir(src, dst string) error {
	if _, err := os.Stat(src); err != nil {
		return fmt.Errorf("source directory does not exist: %s", src)
	}

	if err := os.MkdirAll(dst, 0755); err != nil {
		return err
	}

	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}

		dstPath := filepath.Join(dst, relPath)

		if info.IsDir() {
			return os.MkdirAll(dstPath, info.Mode())
		}

		// Only copy .basl files
		if !strings.HasSuffix(path, ".basl") {
			return nil
		}

		return copyFile(path, dstPath)
	})
}

func copyFile(src, dst string) error {
	srcFile, err := os.Open(src)
	if err != nil {
		return err
	}
	defer srcFile.Close()

	dstFile, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer dstFile.Close()

	if _, err := io.Copy(dstFile, srcFile); err != nil {
		return err
	}

	return dstFile.Sync()
}
