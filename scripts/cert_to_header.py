#!/usr/bin/env python3
"""Convert PEM certificate and private key to a C header with DER byte arrays.

Usage:
    cert_to_header.py <cert.pem> <key.pem> <output.h>
"""

import base64
import sys


def pem_to_der(pem_text: str) -> bytes:
    """Extract DER bytes from the first PEM block in pem_text."""
    lines = pem_text.strip().splitlines()
    b64 = "".join(line for line in lines if not line.startswith("-----"))
    return base64.b64decode(b64)


def der_to_c_array(name: str, data: bytes) -> str:
    """Return a C static byte-array definition for *data*."""
    rows = []
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in chunk))
    body = ",\n".join(rows)
    return (
        f"static const unsigned char {name}[] = {{\n"
        f"{body}\n"
        f"}};\n"
        f"static const size_t {name}_len = {len(data)}u;\n\n"
    )


def main() -> None:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} cert.pem key.pem output.h", file=sys.stderr)
        sys.exit(1)

    cert_pem = open(sys.argv[1]).read()
    key_pem = open(sys.argv[2]).read()

    cert_der = pem_to_der(cert_pem)
    key_der = pem_to_der(key_pem)

    with open(sys.argv[3], "w") as f:
        f.write("/* Auto-generated TLS test certificate — do not edit.\n")
        f.write(" * Produced by scripts/cert_to_header.py at configure time.\n")
        f.write(" */\n")
        f.write("#pragma once\n\n")
        f.write(der_to_c_array("vigil_test_tls_cert_der", cert_der))
        f.write(der_to_c_array("vigil_test_tls_key_der", key_der))


if __name__ == "__main__":
    main()
