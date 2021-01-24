#!/usr/bin/python3



import os
import shutil
import struct
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
            (int(entries[13].strip(), 16) if   # lower case code point, or -1
                len(entries[13].strip()) > 0 else -1),
            (int(entries[12].strip(), 16) if   # upper case code point, or -1
                len(entries[12].strip()) > 0 else -1)
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
    f.write("static int64_t _widechartbl_lowest_cp = " +
            str(smallest_cp_seen) + "LL;\n\n")
    f.write("static int64_t _widechartbl_highest_cp = " +
            str(highest_cp_seen) + "LL;\n\n")
    f.write("static int64_t _widechartbl_arraylen = " +
            str((highest_cp_seen - smallest_cp_seen) + 1) + ";")
    f.write("extern uint8_t *_widechartbl_ismodifier;")
    f.write("extern uint8_t *_widechartbl_istag;");
    f.write("extern int64_t *_widechartbl_lowercp;");
    f.write("extern int64_t *_widechartbl_uppercp;");

arraylen = (highest_cp_seen - smallest_cp_seen) + 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data___widechartbl_ismodifier.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        f.write(struct.pack(
            "<B", 1 if i in chars and chars[i][2] else 0
        ))
        i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data___widechartbl_istag.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        f.write(struct.pack(
            "<B", 1 if i in chars and chars[i][3] else 0
        ))
        i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data___widechartbl_lowercp.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        if i in chars:
            f.write(struct.pack("<q", chars[i][4]))
        else:
            f.write(struct.pack("<q", -1))
        i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data___widechartbl_uppercp.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        if i in chars:
            f.write(struct.pack("<q", chars[i][5]))
        else:
            f.write(struct.pack("<q", -1))
        i += 1

for fname in os.listdir(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode")):
    if fname.startswith("unicode_data__") and fname.endswith(".dat"):
        src_path = os.path.join(
            os.path.abspath(os.path.dirname(__file__)),
            "..", "vendor", "unicode", fname)
        dest_path = os.path.join(
            os.path.abspath(os.path.dirname(__file__)),
            "..", "horse_modules_builtin", fname)
        if os.path.exists(dest_path):
            os.remove(dest_path)
        shutil.copyfile(src_path, dest_path)

print("Generated.")
