#!/usr/bin/python3

import os
import sys
import textwrap

# Look at the compiler we're using (=cross-compilation):
cc_var = os.environ.get("CC", "gcc")
if cc_var is None or len(cc_var) == "":
    cc_var = "gcc"
is_crosscompile_host = ("-gcc" in cc_var.lower())
is_mingw = ("mingw" in cc_var.lower())

# Change to directory: 
os.chdir(os.path.join(
    os.path.dirname(__file__), "..", "vendor", "physfs"
))

# Collect source files:
sourcefiles = []
for f in os.listdir("src/"):
    if not f.endswith(".c") and \
            not f.endswith(".cpp") and \
            not f.endswith(".h"):
        continue
    full_path = "src/" + f
    if os.path.isdir(full_path):
        continue
    # For windows, skip the non-windows source files:
    if "platform" in f and is_mingw and "windows" not in f:
        continue
    elif "platform" in f and not is_mingw and "windows" in f:
        continue
    sourcefiles.append("src/" + f)

# Echo main flags:
print(
    "\nSHELL:=bash" +
    "\nCFLAGS:=-O2" +
    "\nLIBS:=" + ("-ldl -lm" if not is_mingw else "") +
    "\nSOURCE:=" + " ".join(sourcefiles) +
    "\nOBJECTS:=" + " ".join([
        f.rpartition(".")[0] + ".o" for f in sourcefiles
        if not f.endswith(".h")
    ]) +
    "\n"
)
if is_crosscompile_host:
    print(textwrap.dedent("""\
          ARTOOL:=$(shell echo -e 'print("'$(CC)'".rpartition("-")[0])' | python3)-ar
          """))
else:
    print(textwrap.dedent("""\
          ARTOOL:=ar
          """))

# Echo targets for object files:
print("all: $(OBJECTS)\n\t$(ARTOOL) rcs libphysfs.a $(OBJECTS)")
print("clean:\n\trm -f $(OBJECTS) src/*.o")
for f in sourcefiles:
    if f.endswith(".h"):
        continue
    print(f.rpartition(".")[0] + ".o: " + f)
    print("\t$(CC) $(CFLAGS) " + f +
          " -c -o " + f.rpartition(".")[0] + ".o" +
          " $(LIBS)\n")
