package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"

	basllsp "github.com/bluesentinelsec/basl/pkg/basl/lsp"
)

func runLSP(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("lsp"))
		return 0
	}
	if len(args) != 0 {
		fmt.Fprintln(os.Stderr, "usage: basl lsp")
		return 2
	}
	err := basllsp.Serve(context.Background(), os.Stdin, os.Stdout)
	if err != nil && !errors.Is(err, io.EOF) && !errors.Is(err, context.Canceled) {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	return 0
}
