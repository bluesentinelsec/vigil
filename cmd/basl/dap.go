package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"

	basldap "github.com/bluesentinelsec/basl/pkg/basl/dap"
)

func runDAP(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("dap"))
		return 0
	}
	if len(args) != 0 {
		fmt.Fprintln(os.Stderr, "usage: basl dap")
		return 2
	}
	err := basldap.Serve(context.Background(), os.Stdin, os.Stdout)
	if err != nil && !errors.Is(err, io.EOF) && !errors.Is(err, context.Canceled) {
		fmt.Fprintf(os.Stderr, "error: %s\n", err)
		return 1
	}
	return 0
}
