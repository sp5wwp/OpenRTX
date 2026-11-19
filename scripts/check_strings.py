#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Checks that EnglishStrings.h sets every field of stringsTable_t
(openrtx/include/ui/ui_strings.h), and no other.

A MISSING field is a real bug: the table uses designated initializers, so an
unset field is a NULL const char*, which the UI and the voice prompts
dereference the first time the string is shown or spoken. An EXTRA field
fails to compile, but is reported here by name.

Exit status is non-zero if any field is missing or extra.
"""

import re
import sys
from pathlib import Path

UI_DIR = Path(__file__).resolve().parent.parent / "openrtx" / "include" / "ui"
SOURCE_FILE = "EnglishStrings.h"
STRUCT_FILE = "ui_strings.h"
FIELD_RE = re.compile(r"^\s*\.(\w+)\s*=")
STRUCT_FIELD_RE = re.compile(r"^\s*const\s+char\*\s*(\w+)\s*;")


def extract_fields(path: Path) -> list[str]:
    """Parse the designated-initializer field names out of a string table."""
    fields = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            m = FIELD_RE.match(line)
            if m:
                fields.append(m.group(1))
    return fields


def extract_struct_fields(path: Path) -> list[str]:
    """Parse stringsTable_t's own field declaration order out of ui_strings.h."""
    fields = []
    in_struct = False
    with path.open(encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if not in_struct:
                if stripped == "typedef struct":
                    in_struct = True
                continue
            if stripped == "}":
                break
            m = STRUCT_FIELD_RE.match(line)
            if m:
                fields.append(m.group(1))
    return fields


def main() -> int:
    """Compare the English table with the struct; return the exit status."""
    struct_path = UI_DIR / STRUCT_FILE
    source_path = UI_DIR / SOURCE_FILE

    struct_fields = extract_struct_fields(struct_path)
    if not struct_fields:
        print(f"error: no fields parsed from stringsTable_t in {struct_path}", file=sys.stderr)
        return 2

    fields = extract_fields(source_path)
    if not fields:
        print(f"error: no fields parsed from {source_path}", file=sys.stderr)
        return 2

    missing = [f for f in struct_fields if f not in fields]
    extra = [f for f in fields if f not in struct_fields]

    if missing:
        print(f"{SOURCE_FILE}: MISSING {len(missing)} field(s) present in stringsTable_t ({STRUCT_FILE}):")
        print("  (these will be NULL const char* at runtime -- crash risk when displayed/spoken):")
        for f in missing:
            print(f"  - {f}")

    if extra:
        print(f"{SOURCE_FILE}: has {len(extra)} field(s) not present in stringsTable_t ({STRUCT_FILE}):")
        for f in extra:
            print(f"  - {f}")

    if missing or extra:
        return 1

    print(f"{SOURCE_FILE}: OK, matches stringsTable_t ({len(fields)} fields)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
