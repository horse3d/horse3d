#!/usr/bin/python3

import os
import shutil

for f in os.listdir("."):
    if "_gmake_" in f and f.endswith(".a") and not os.path.isdir(f):
        shutil.move(f, f.partition("_gmake_")[0] + ".a")
