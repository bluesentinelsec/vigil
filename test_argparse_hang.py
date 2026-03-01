#!/usr/bin/env python3
"""
Test for args.ArgParser hang issue (GitHub issue #12)
Uses pexpect to detect if parse() hangs waiting for input.
"""

import pexpect
import sys
import os

def test_argparse_basic():
    """Test that ArgParser.parse() doesn't hang with basic flags"""
    
    # Create test script
    test_script = """import "args";
import "fmt";

fn main() -> i32 {
    ArgParser parser = args.parser("test", "Test program");
    err e1 = parser.flag("verbose", "bool", "false", "verbose mode");
    err e2 = parser.flag("output", "string", "out.txt", "output file");
    
    fmt.eprintln("Calling parse()...");
    map<string, string> result, err e = parser.parse();
    
    if (e != ok) {
        fmt.eprintln(f"Error: {e}");
        return 1;
    }
    
    string verbose = result["verbose"];
    string output = result["output"];
    fmt.println(f"verbose={verbose}, output={output}");
    return 0;
}
"""
    
    with open('/tmp/test_argparse_hang.basl', 'w') as f:
        f.write(test_script)
    
    # Test with flags
    print("Test 1: With --verbose flag")
    child = pexpect.spawn('./basl /tmp/test_argparse_hang.basl --verbose', 
                          timeout=3, 
                          cwd=os.getcwd())
    
    try:
        child.expect('Calling parse\\(\\)\\.\\.\\.')
        child.expect('verbose=true')
        child.expect(pexpect.EOF)
        print("✓ PASS: ArgParser.parse() completed successfully with flags")
    except pexpect.TIMEOUT:
        print("✗ FAIL: ArgParser.parse() hung (timeout after 3s)")
        child.kill(9)
        return False
    except pexpect.EOF:
        print("✗ FAIL: Unexpected EOF")
        return False
    
    # Test without flags
    print("\nTest 2: Without flags (defaults)")
    child = pexpect.spawn('./basl /tmp/test_argparse_hang.basl', 
                          timeout=3,
                          cwd=os.getcwd())
    
    try:
        child.expect('Calling parse\\(\\)\\.\\.\\.')
        child.expect('verbose=false')
        child.expect(pexpect.EOF)
        print("✓ PASS: ArgParser.parse() completed successfully with defaults")
    except pexpect.TIMEOUT:
        print("✗ FAIL: ArgParser.parse() hung (timeout after 3s)")
        child.kill(9)
        return False
    
    # Test with positional args
    print("\nTest 3: With positional arguments")
    test_script_pos = """import "args";
import "fmt";

fn main() -> i32 {
    ArgParser parser = args.parser("test", "Test program");
    err e1 = parser.arg("input", "string", "input file");
    
    fmt.eprintln("Calling parse()...");
    map<string, string> result, err e = parser.parse();
    
    if (e != ok) {
        fmt.eprintln(f"Error: {e}");
        return 1;
    }
    
    string input = result["input"];
    fmt.println(f"input={input}");
    return 0;
}
"""
    
    with open('/tmp/test_argparse_pos.basl', 'w') as f:
        f.write(test_script_pos)
    
    child = pexpect.spawn('./basl /tmp/test_argparse_pos.basl myfile.txt', 
                          timeout=3,
                          cwd=os.getcwd())
    
    try:
        child.expect('Calling parse\\(\\)\\.\\.\\.')
        child.expect('input=myfile.txt')
        child.expect(pexpect.EOF)
        print("✓ PASS: ArgParser.parse() completed successfully with positional args")
    except pexpect.TIMEOUT:
        print("✗ FAIL: ArgParser.parse() hung (timeout after 3s)")
        child.kill(9)
        return False
    
    return True

if __name__ == '__main__':
    print("Testing args.ArgParser for hang issue (GitHub #12)")
    print("=" * 60)
    
    if not os.path.exists('./basl'):
        print("Error: ./basl binary not found. Run 'make build' first.")
        sys.exit(1)
    
    success = test_argparse_basic()
    
    print("\n" + "=" * 60)
    if success:
        print("RESULT: All tests passed - ArgParser does NOT hang")
        print("\nConclusion: Issue #12 may have been a misunderstanding or")
        print("the hang occurred in a different context (e.g., incorrect usage).")
        sys.exit(0)
    else:
        print("RESULT: ArgParser DOES hang - issue confirmed")
        sys.exit(1)
