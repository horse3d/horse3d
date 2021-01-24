#!/usr/bin/python3

import os
import struct
import sys

if len(sys.argv) <= 1:
    print("error: please specify target binary", file=sys.stderr)
    sys.exit(1)
if len(sys.argv) <= 2:
    print("error: please specify .h64pak path", file=sys.stderr)
    sys.exit(1)

tgfile = os.path.abspath(sys.argv[1])
pakfile = os.path.abspath(sys.argv[2])

os.chdir(os.path.dirname(os.path.abspath(__file__)))

if not os.path.exists(tgfile):
    print("error: no such file: " + tgfile)
    sys.exit(1)

if not os.path.exists(pakfile):
    print("error: no such file: " + pakfile)
    sys.exit(1)

databin = None
with open(tgfile, "rb") as f:
    databin = f.read()

datapak = None
with open(pakfile, "rb") as f:
    datapak = f.read()

pak_start = len(databin)
pak_end = pak_start + len(datapak)

databin += (
    datapak + struct.pack("<q", pak_start) + struct.pack("<q", pak_end) +
    b"\x00\xFF\x00H64PAKAPPEND_V1\x00\xFF\x00"
)

with open(tgfile, 'wb') as f:
    f.write(databin)
