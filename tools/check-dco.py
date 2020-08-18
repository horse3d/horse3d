#!/usr/bin/python3

import subprocess
import sys

try:
    output = subprocess.check_output([
        "git", "log", "-1", "--pretty=%B"
    ])
except subprocess.CalledProcessError as e:
    print("check-dco.py: error: failed to check commit "
          "message for DCO.", file=sys.stderr)
    print("check-dco.py: error: git log output follows:",
          file=sys.stderr)
    output = e.output
    try:
        output = output.decode("utf-8", "replace")
    except AttributeError:
        pass
    print(output, file=sys.stderr)
    sys.exit(1)

try:
    output = output.decode("utf-8", "replace")
except AttributeError:
    pass
# Check if we can find valid sign:
for l in output.split("\n"):
    l = l.strip()
    if l.strip().startswith("DCO-1.1-Signed-off-by: ") and \
            "<" in l and "@" in l and ">" in l:
        print("check-dco.py: info: signed-off-by found.", file=sys.stderr)
        sys.exit(0)
# If not, check if we can find likely invalid sign:
for l in output.split("\n"):
    l = l.strip()
    if "signed" in l.lower() and "off" in l.lower():
        print(
            "check-dco.py: error: commit sign-off must adhere to "
            "docs/Contributing.md#developer-certificate-of-origin",
            file=sys.stderr
        )
        sys.exit(1)
# If not, output final error:
print("check-dco.py: error: no sign-off found. Please read "
      "docs/Contributing.md#developer-certificate-of-origin",
      file=sys.stderr)
sys.exit(1)
