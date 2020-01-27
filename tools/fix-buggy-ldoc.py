#!/usr/bin/python3

import os
import shutil
import sys
import textwrap


if len(sys.argv) < 2 or \
        not os.path.exists(sys.argv[1]) or \
        os.path.isfile(sys.argv[1]):
    print("error: please supply documentation output directory",
          file=sys.stderr)
    sys.exit(1)

section_order = ["Modules", "Topics", "Classes"]

def fix_doctype(contents):
    doctype_idx = contents.find("<!DOCTYPE ")
    doctype_idx_end = -1
    if doctype_idx >= 0:
        doctype_idx_end = contents[doctype_idx:].find(">")
        if doctype_idx_end >= 0:
            doctype_idx_end += doctype_idx + len("/>")
    if doctype_idx >= 0 and doctype_idx_end >= 0:
        return contents[:doctype_idx] + "<!DOCTYPE HTML>" +\
            contents[doctype_idx_end:]
    return contents

def fix_section_order(contents, section1, section2):
    # Fix cases where "Classes" heading comes before "Modules":
    sec1_idx = contents.find("<h2>" + str(section1) + "</h2>")
    sec1_idx_end = -1
    if sec1_idx >= 0:
        sec1_idx_end = contents[sec1_idx:].find("</ul>")
        if sec1_idx_end >= 0:
            sec1_idx_end += sec1_idx + len("</ul>")
    sec2_idx = contents.find("<h2>" + str(section2) + "</h2>")
    sec2_idx_end = -1
    if sec2_idx >= 0:
        sec2_idx_end = contents[sec2_idx:].find("</ul>")
        if sec2_idx_end >= 0:
            sec2_idx_end += sec2_idx + len("</ul>")
            if sec2_idx_end < len(contents) and \
                    contents[sec2_idx_end] == '\n':
                sec2_idx_end += 1
    if sec2_idx < sec1_idx and sec1_idx >= 0 and \
            sec1_idx_end >= 0 and sec2_idx_end >= 0:
        wrong_block = contents[sec2_idx:sec2_idx_end]
        contents = contents[:sec2_idx] +\
            contents[sec2_idx_end:
                     sec1_idx_end] +\
            wrong_block + contents[sec1_idx_end:]
    return contents

def fix_nonfloating_contents_box(contents):
    # Fix cases where the "Contents" box isn't floating:
    contents_idx = contents.find("<h2>Contents</h2>")
    contents_idx_end = -1
    if contents_idx >= 0:
        contents_idx_end = contents[contents_idx:].find("</ul>")
        if contents_idx_end >= 0:
            contents_idx_end += contents_idx + len("</ul>")
    prefix = textwrap.dedent("""\
        <div id='content-listing'
             style='position:absolute; right:10px; top:10px; width:200px;
             background-color:#fff; position:fixed;
             border:2px solid #55b;'>""")
    if contents_idx >= 0 and contents_idx_end > contents_idx and \
            prefix not in contents[contents_idx - len(prefix) * 2:
                                   contents_idx]:
        contents = contents[:contents_idx] +\
            prefix + contents[contents_idx:contents_idx_end] +\
            "</div>" + contents[contents_idx_end:]
    return contents

def fix_missing_index_entry(contents):
    modules_idx = contents.find("<h2>Modules</h2>")
    index_idx = contents.find(">Index</")
    if index_idx < 0 and modules_idx > 0:
        contents = contents[:modules_idx] +\
            "<ul><li><strong>Index</strong></li></ul>\n\n" +\
            contents[modules_idx:]
    return contents


for root, dirs, files in os.walk(sys.argv[1], topdown=False):
    for filename in files:
        if not filename.endswith(".html"):
            continue

        full_path = os.path.join(root, filename)
        with open(full_path, "r", encoding="utf-8") as f:
            old_contents = f.read()
            contents = old_contents.replace("\r\n", "\n").replace("\r", "\n")

        contents = fix_doctype(contents)
        for sec1 in section_order:
            for sec2 in section_order[section_order.index(sec1) + 1:]:
                contents = fix_section_order(contents, sec1, sec2) 
        contents = fix_nonfloating_contents_box(contents)
        contents = fix_missing_index_entry(contents)
        if contents != old_contents:
            print("Fixing " + str(full_path) + "...")
            with open(full_path, "w", encoding="utf-8") as f:
                f.write(contents)
print("Fixed all documentation.")

