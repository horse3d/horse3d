#!/usr/bin/python3

import os
import shlex
import sys

CXX = os.environ.get("WRAPPEDCXX", "")
if CXX == "":
    raise RuntimeError("WRAPPEDCXX not set")
args = [a for a in sys.argv[1:] if not a.startswith("-msse") and a != "-m64"]
call = [CXX] + args
print("WRAPPEDCXX: " + str(call), file=sys.stderr, flush=True)
val = os.system(" ".join([shlex.quote(c) for c in call]))
os._exit(val >> 8)
