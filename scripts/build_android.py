#!/usr/bin/env python3
"""Android build entrypoint reserved for the post-ROM port integration."""
from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NDK_VERSION = "28.2.13676358"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--package-only", action="store_true")
    args = parser.parse_args()
    if not args.sdk:
        parser.error("set ANDROID_HOME/ANDROID_SDK_ROOT or pass --sdk")
    sdk = Path(args.sdk).resolve()
    toolchain = sdk / "ndk" / NDK_VERSION / "build/cmake/android.toolchain.cmake"
    if not toolchain.is_file():
        parser.error(f"Android NDK {NDK_VERSION} is not installed: {toolchain}")
    if not (ROOT / "World Driver Championship (USA).z64").is_file() and not args.package_only:
        parser.error("place your own USA ROM at the repository root before translating")
    if not (ROOT / "android" / "gradlew").is_file():
        parser.error("Android project scaffold is not installed yet; finish the desktop ROM port first")
    if not shutil.which("cmake"):
        parser.error("cmake is required")
    parser.error("Android packaging is gated until the WDC native bridge exists")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
