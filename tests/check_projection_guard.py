"""Require deliberate rejection of malformed descriptor addresses."""
import subprocess
import sys

for address in ("00000000", "a0000400", "807ffff4", "80400101"):
    result = subprocess.run([*sys.argv[1:], "--invalid-descriptor", address],
                            capture_output=True, text=True, timeout=5)
    if result.returncode != 73 or f"invalid native guest range: {address} + 18" not in result.stderr:
        raise SystemExit(f"Descriptor {address} was not rejected by the range check: {result}")
