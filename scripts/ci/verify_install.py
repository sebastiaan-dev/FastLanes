# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# scripts/ci/verify_install.py
# ────────────────────────────────────────────────────────
#!/usr/bin/env python3
"""Check that every public C/C++ header is present in an install tree."""

import argparse
from pathlib import Path


def missing_headers(source: Path, installed: Path) -> list[Path]:
    if not source.is_dir():
        raise ValueError(f"Source include directory does not exist: {source}")
    if not installed.is_dir():
        raise ValueError(f"Installed include directory does not exist: {installed}")
    headers = sorted(
        path.relative_to(source)
        for path in source.rglob("*")
        if path.is_file() and path.suffix in (".h", ".hpp")
    )
    if not headers:
        raise ValueError(f"No public headers found in {source}")
    return [path for path in headers if not (installed / path).is_file()]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("installed", type=Path)
    args = parser.parse_args()
    try:
        missing = missing_headers(args.source, args.installed)
    except ValueError as error:
        print(error)
        return 1
    if missing:
        print("Missing installed headers:")
        for path in missing:
            print(f"  {path}")
        return 1
    print("All public headers are installed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
