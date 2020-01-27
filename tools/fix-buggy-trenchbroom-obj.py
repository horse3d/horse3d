#!/usr/bin/python3

import argparse
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument(
    "folder"
)

args = parser.parse_args()
args.folder = os.path.abspath(args.folder)

if not os.path.exists(args.folder):
    print("error: no such file or dir: " + str(args.folder),
          file=sys.stderr)
    sys.exit(1)


def fix_obj(path):
    with open(path, "r", encoding="utf-8") as f:
        contents = f.read()
    if not "newmtl" in contents:
        return
    print("Fixing " + str(path) + "...")
    content_lines = contents.splitlines()
    last_material_start = -1
    last_material_name = None
    last_material_had_mapkd = False
    i = 0
    while i <= len(content_lines):
        if i >= len(content_lines) or \
                content_lines[i].startswith("newmtl "):
            if last_material_start >= 0 and \
                    not last_material_had_mapkd:
                fname = last_material_name
                if not fname.lower().endswith(".png"):
                    fname += ".png"
                if not fname.lower().startswith("textures/"):
                    fname = "textures/" + fname
                content_lines = (
                    content_lines[:i] + ["map_Kd " + fname] +
                    content_lines[i:]
                )
                i += 1
        if i < len(content_lines) and \
                content_lines[i].lower().startswith("map_kd"):
            last_material_had_mapkd = True
        if i < len(content_lines) and \
                content_lines[i].lower().startswith("newmtl "):
            last_material_name = (
                content_lines[i][len("newmtl "):].strip()
            )
            last_material_start = i
            last_material_had_mapkd = False
        i += 1
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(content_lines))


if os.path.isdir(args.folder):
    for root, dirs, files in os.walk(args.folder):
        for f in files:
            full_path = os.path.join(root, f)
            if os.path.exists(full_path) and \
                    full_path.lower().endswith(".mtl"):
                fix_obj(full_path)
elif args.folder.lower().endswith(".mtl"):
    fix_obj(args.folder)
else:
    print("error: unknown file type: " + str(args.folder),
          file=sys.stderr)
    sys.exit(1)
