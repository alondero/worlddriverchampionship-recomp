"""Compile and run host-only rational cadence checks using the reference toolchain."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", required=True, type=Path)
    args = parser.parse_args()
    cache = (args.reference_build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    here = Path(__file__).resolve().parent
    output = here.parents[1] / "build/render-split-prototype"
    output.mkdir(parents=True, exist_ok=True)
    exe = output / "cadence_test.exe"
    subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(here.parents[1] / "src"), str(here.parents[1] / "tests/test_render_cadence.cpp"), "-o", str(exe)], check=True)
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([str(exe)], check=True, env=env)


if __name__ == "__main__":
    main()
