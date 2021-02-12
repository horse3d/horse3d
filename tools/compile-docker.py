#!/usr/bin/python3

import os
import shutil
import subprocess
import sys
import tempfile

IMAGE_LBL = "horse64-3hg290g"
CONTAINER_LBL = "horse64-r0y30i4jioog"
RUN_TESTS = False
RUN_BASH = False
USE_CACHE = False

if "--help" in sys.argv[1:]:
    print("Runs build in docker. Use --run-tests to also run tests.")
    sys.exit(0)
if "--run-tests" in sys.argv[1:]:
    RUN_TESTS = True
if "--shell" in sys.argv[1:]:
    RUN_BASH = True
if "--cache-image" in sys.argv[1:]:
    USE_CACHE = True

tools_dir = os.path.abspath(os.path.dirname(__file__))
os.chdir(os.path.join(os.path.dirname(__file__), ".."))

print("SETTINGS:", file=sys.stderr, flush=True)
print("=========", file=sys.stderr, flush=True)
print(" Run tests: " + str(RUN_TESTS).lower(), file=sys.stderr, flush=True)
print(" Run bash: " + str(RUN_BASH).lower(), file=sys.stderr, flush=True)
print(" Cache image: " + str(USE_CACHE).lower(),
      file=sys.stderr, flush=True)
print("", file=sys.stderr, flush=True)

subprocess.check_output(["docker", "ps"])  # test docker access
containers = subprocess.check_output([
    "docker", "ps", "-aq", "-f", "label=" + CONTAINER_LBL
]).decode("utf-8", "replace").splitlines()
for container in containers:
    subprocess.run(
        ["docker", "stop", container],
        stderr=subprocess.STDOUT
    )
    subprocess.run(
        ["docker", "rm", container],
        stderr=subprocess.STDOUT
    )
sys.stdout.flush()
BUILD_IMAGE=True
img_path = None
if USE_CACHE:
    img_folder = os.path.abspath(os.path.expanduser(
        "~/.local/share/core.horse64.org-dockerimage/"
    ))
    os.makedirs(img_folder, exist_ok=True)
    print("Checking image cache folder: " + str(img_folder), file=sys.stderr)
    img_path = os.path.join(img_folder, "image.tar")
    restored = False
    if os.path.exists(os.path.join(img_folder, "image.tar")):
        try:
            print("Attempting image restore...", file=sys.stderr)
            subprocess.run(
                ["docker", "import", img_path, IMAGE_LBL + ":" + IMAGE_LBL],
                stderr=subprocess.STDOUT
            ).check_returncode()
            BUILD_IMAGE=False
            restored = True
            print("Building properly labelled image clone...",
                  file=sys.stderr)
            subprocess.run([
                "docker", "tag", IMAGE_LBL, IMAGE_LBL + "img"
            ], stderr=subprocess.STDOUT).check_returncode()
            print("Restored image from " + str(img_path) + "!", file=sys.stderr)
        except subprocess.CalledProcessError:
            pass
    if not restored:
        print("No image restored from cache.")
if BUILD_IMAGE:
    if os.path.exists(".dockerignore"):
        os.remove(".dockerignore")
    shutil.copyfile(
        os.path.join(tools_dir, "dockerignore"),
        ".dockerignore"
    )
    try:
        subprocess.run(
            ["docker", "build", "-t", IMAGE_LBL, "-f",
            os.path.join(tools_dir, "Dockerfile"), "."],
            stderr=subprocess.STDOUT
        ).check_returncode()
    finally:
        sys.stdout.flush()
        if os.path.exists(".dockerignore"):
            os.remove(".dockerignore")
    if USE_CACHE:
        assert(img_path != None)
        print("Attempting image save...")
        subprocess.run(
            ["docker", "save", "-o", img_path, IMAGE_LBL],
            stderr=subprocess.STDOUT
        ).check_returncode()
        print("Saved image to: " + str(img_path))
if not os.path.exists("binaries"):
    os.mkdir("binaries")
args = (
    ["docker", "run"] +
    (["-ti"] if RUN_BASH else []) +
    ["--label", CONTAINER_LBL,
    "-e", "RUN_TESTS=" + ("yes" if RUN_TESTS else "no"),
    "-v",
    os.path.abspath("./binaries/") +
    ":/compile-tree/binaries/:rw,z", IMAGE_LBL] +
    (["/bin/bash"] if RUN_BASH else ["/do-build"])
)

try:
    subprocess.run(
        args,
        stderr=subprocess.STDOUT
    ).check_returncode()
    sys.stdout.flush()
except subprocess.CalledProcessError as e:
    sys.stdout.flush()
    print("Shutting down due to error:", file=sys.stderr, flush=True)
    raise e
