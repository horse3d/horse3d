#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
import textwrap

try:
    dirpath = tempfile.mkdtemp()
    with open(os.path.join(dirpath, "main.c"), "w") as f:
        f.write(textwrap.dedent("""\
            int main(int argc, const char **argv) {
                return 0;
            }
        """))
    subprocess.check_output(
        ["gcc", "-msse2", "-o", "test", "main.c"],
        cwd=dirpath, stderr=subprocess.STDOUT,
    )
    print("yes")
    sys.exit(0)
except subprocess.CalledProcessError as e:
    output = e.output
    try:
        output = output.decode("utf-8", "replace")
    except AttributeError:
        pass
    if (output.find("unrecognized") >= 0):
        print("no")
        sys.exit(0)
    print("error: " + str(output))
    sys.exit(1)

