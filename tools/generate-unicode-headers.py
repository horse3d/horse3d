#!/usr/bin/python3



import os
import sys
import textwrap

if os.path.exists(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data_header.h")
        ):
    print("Unicode(R) data header exists. Remove this file to regenerate:")
    print("    vendor/unicode/unicode_data_header.h")
    sys.exit(0)

print("Generating Unicode(R) data header...")

chars = dict()

smallest_cp_seen = None
highest_cp_seen = None

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "UnicodeData.txt"),
        "r", encoding="utf-8") as f:
    lines = f.readlines()
    print(str(len(lines)) + " line(s).")
    for l in lines:
        if l.startswith("#") or l.find(";") < 0:
            continue
        entries = l.split(";")
        if len(entries) < 14:
            continue
        if "PRIVATE USE" in entries[1].strip().upper():
            continue
        codepoint = int(entries[0], 16)
        chars[codepoint] = (
            codepoint,
            entries[1].strip(),   # name
            (True if entries[1].strip().upper().   # modifier (true/false)
                startswith("MODIFIER LETTER") else False),
            (True if entries[1].strip().upper().   # tag (true/false)
                startswith("TAG ") else False),
            (int(entries[12].strip(), 16) if   # lower case code point, or -1
                len(entries[12].strip()) > 0 else -1),
            (int(entries[13].strip(), 16) if   # upper case code point, or -1
                len(entries[13].strip()) > 0 else -1)
        )
        #print(str(chars[codepoint]))
        if smallest_cp_seen is None or codepoint < smallest_cp_seen:
            smallest_cp_seen = codepoint
        if highest_cp_seen is None or codepoint > highest_cp_seen:
            highest_cp_seen = codepoint

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data_header.h"),
        "w", encoding="utf-8") as f:
    f.write(textwrap.dedent("""\
    // UnicodeData.txt-based header as generated for core.horse64.org.
    // Original data still belongs to Unicode(R) Consortium, see accompanied
    // license files. This just transforms it for easier use by
    // horsec/horsevm.

    #include <stdint.h>

    """))
    arraylen = (highest_cp_seen - smallest_cp_seen) + 1
    f.write("static int64_t _widechartbl_highest_cp = " +
            str(highest_cp_seen) + "LL;\n\n")
    f.write("static uint8_t _widechartbl_ismodifier[] = {\n    ")
    ctr = 0
    i = smallest_cp_seen
    while i < highest_cp_seen:
        if i > smallest_cp_seen:
            f.write(",")
        ctr += 1
        if ctr > 37:
            f.write("\n    ")
            ctr = 1
        if i in chars:
            f.write("1" if chars[i][2] else "0")
        else:
            f.write("0")
        i += 1
    f.write("\n};\n")
    f.write("static uint8_t _widechartbl_istag[] = {\n    ")
    ctr = 0
    i = smallest_cp_seen
    while i < highest_cp_seen:
        if i > smallest_cp_seen:
            f.write(",")
        ctr += 1
        if ctr > 37:
            f.write("\n    ")
            ctr = 1
        if i in chars:
            f.write("1" if chars[i][3] else "0")
        else:
            f.write("0")
        i += 1
    f.write("\n};\n")
    f.write("static int64_t _widechartbl_lowercp[] = {\n    ")
    ctr = 0
    i = smallest_cp_seen
    while i < highest_cp_seen:
        if i > smallest_cp_seen:
            f.write(",")
        ctr += 1
        if ctr > 14:
            f.write("\n    ")
            ctr = 1
        if i in chars:
            f.write(str(chars[i][4]) + "LL")
        else:
            f.write("-1LL")
        i += 1
    f.write("\n};\n")
    f.write("static int64_t _widechartbl_uppercp[] = {\n    ")
    ctr = 0
    i = smallest_cp_seen
    while i < highest_cp_seen:
        if i > smallest_cp_seen:
            f.write(",")
        ctr += 1
        if ctr > 14:
            f.write("\n    ")
            ctr = 1
        if i in chars:
            f.write(str(chars[i][5]) + "LL")
        else:
            f.write("-1LL")
        i += 1
    f.write("\n};\n")

print("Generated.")