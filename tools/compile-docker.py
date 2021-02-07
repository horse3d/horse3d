#!/usr/bin/python3

import os
import shutil
import subprocess
import sys

IMAGE_LBL = "horse64-3hg290g"
CONTAINER_LBL = "horse64-r0y30i4jioog"
RUN_TESTS = False

if "--help" in sys.argv[1:]:
    print("Runs build in docker. Use --run-tests to also run tests.")
    sys.exit(0)
if "--run-tests" in sys.argv[1:]:
    RUN_TESTS = True
if "--shell" in sys.argv[1:]:
    RUN_BASH = True

tools_dir = os.path.abspath(os.path.dirname(__file__))
os.chdir(os.path.join(os.path.dirname(__file__), ".."))

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
    if os.path.exists(".dockerignore"):
        os.remove(".dockerignore")
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
    (["/bin/bash"] if RUN_BASH else [])
)

subprocess.run(
    args,
    stderr=subprocess.STDOUT
).check_returncode()
