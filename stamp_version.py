#!/usr/bin/env python3
"""Stamp version.yaml for development bumps or releases."""

from __future__ import annotations

import pathlib
import re
import sys

VERSION_KEY = "firmware_version"
BUILD_KEY = "firmware_build"
SUFFIX_KEY = "firmware_suffix"


def _sub(text: str, key: str, value: str) -> str:
    pattern = rf'{key}:\s*"[^"]*"'
    if not re.search(pattern, text):
        raise SystemExit(f"{key} not found in version.yaml")
    return re.sub(pattern, f'{key}: "{value}"', text, count=1)


def _get(text: str, key: str) -> str:
    match = re.search(rf'{key}:\s*"([^"]*)"', text)
    if not match:
        raise SystemExit(f"{key} not found in version.yaml")
    return match.group(1)


def _id(text: str) -> str:
    return _get(text, VERSION_KEY) + _get(text, SUFFIX_KEY)


def bump(text: str) -> str:
    build = int(_get(text, BUILD_KEY)) + 1
    text = _sub(text, BUILD_KEY, str(build))
    return _sub(text, SUFFIX_KEY, f"-{build}")


def release(text: str, version: str | None) -> str:
    if version:
        if not re.fullmatch(r"v?\d+\.\d+\.\d+", version):
            raise SystemExit(f"invalid VERSION {version!r}; expected v1.2.3")
        if not version.startswith("v"):
            version = "v" + version
        text = _sub(text, VERSION_KEY, version)
    text = _sub(text, BUILD_KEY, "0")
    return _sub(text, SUFFIX_KEY, "")


def main(argv: list[str]) -> int:
    if len(argv) < 3 or argv[2] not in {"bump", "release"}:
        raise SystemExit("usage: stamp_version.py <version.yaml> bump|release [VERSION]")
    path = pathlib.Path(argv[1])
    text = path.read_text()
    if argv[2] == "bump":
        text = bump(text)
        label = "development"
    else:
        extra = argv[3] if len(argv) > 3 and argv[3] else None
        text = release(text, extra)
        label = "release"
    path.write_text(text)
    print(f"{label} firmware_id -> {_id(text)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
