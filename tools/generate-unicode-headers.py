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
                len(entries[12].strip()) > 0 else -1),
            (True if entries[1].strip().upper().   # combining (true/false)
                startswith("COMBINING ") else False),
        )
        #print(str(chars[codepoint]))
        if smallest_cp_seen is None or codepoint < smallest_cp_seen:
            smallest_cp_seen = codepoint
        if highest_cp_seen is None or codepoint > highest_cp_seen:
            highest_cp_seen = codepoint

arraylen = (highest_cp_seen - smallest_cp_seen) + 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
            "..", "vendor", "unicode",
            "unicode_data___widechartbl_ismodifier.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        f.write(struct.pack(
            "<B", 1 if i in chars and chars[i][2] else 0
        ))
        i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
            "..", "vendor", "unicode",
            "unicode_data___widechartbl_iscombining.dat"),
        "wb") as f:
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        f.write(struct.pack(
            "<B", 1 if i in chars and chars[i][6] else 0
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

graphemebreak_property_values = list()
graphemebreak_property_ranges = dict()

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "GraphemeBreakProperty.txt"),
        "r", encoding="utf-8") as f:
    lines = f.read().splitlines()
    lines = [
        l.strip() for l in lines if not l.strip().startswith("#") and
                        len(l.strip()) > 0 and ";" in l.partition("#")[0]]
    for l in lines:
        if (ord(l[0].upper()) < ord('0') or
                ord(l[0].upper()) > ord('9')) and \
                (ord(l[0].upper()) < ord('A') or
                ord(l[0].upper()) > ord('Z')):
            continue
        cprange = l.partition(";")[0].strip()
        breaktype = (
            l.partition(";")[2].strip().lower().partition("#")[0].strip()
        ).lower()
        if len(breaktype) == 0:
            continue
        range_start_str = cprange.partition("..")[0]
        range_end_str = cprange.partition("..")[2]
        if range_end_str == "":
            range_end_str = range_start_str
        range_start = int(range_start_str, 16)
        range_end = int(range_end_str, 16)
        if breaktype not in graphemebreak_property_values:
            graphemebreak_property_values = sorted(
                graphemebreak_property_values + [breaktype]
            )
        i = range_start
        while i <= range_end:
            idx = graphemebreak_property_values.index(breaktype)
            assert(idx >= 0)
            graphemebreak_property_ranges[i] = (idx + 1)
            i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode",
        "unicode_data___widechartbl_graphemebreaktype.dat"),
        "wb") as f:
    print("Analysing grapheme break ranges, this may take a while...")
    i = smallest_cp_seen
    while i <= highest_cp_seen:
        gbt = 0
        if i in graphemebreak_property_ranges:
            gbt = graphemebreak_property_ranges[i]
            assert(gbt > 0)
        f.write(struct.pack(
            "<B", gbt
        ))
        i += 1

with open(os.path.join(
        os.path.abspath(os.path.dirname(__file__)),
        "..", "vendor", "unicode", "unicode_data_header.h"),
        "w", encoding="utf-8") as f:
    print("Writing final unicode_data_header.h...")
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
            str((highest_cp_seen - smallest_cp_seen) + 1) + ";\n")
    f.write("extern uint8_t *_widechartbl_ismodifier;\n")
    f.write("extern uint8_t *_widechartbl_istag;\n")
    f.write("extern int64_t *_widechartbl_lowercp;\n")
    f.write("extern int64_t *_widechartbl_uppercp;\n")
    f.write("extern uint8_t *_widechartbl_iscombining;\n")
    f.write("extern uint8_t *_widechartbl_graphemebreaktype;\n")
    f.write("\nenum _widechargraphbreaktype {\n")
    f.write("    GBT_NONE = 0,\n")
    n = 1
    for breaktype in graphemebreak_property_values:
        f.write("    GBT_" + str(breaktype).upper() +
                " = " + str(n) + ",\n")
        n += 1
    f.write("    GBT_TOTAL_TYPES = " + str(n) + "\n")
    f.write("};\n")

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
