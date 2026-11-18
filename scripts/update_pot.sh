#!/bin/bash
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerate the translation template from the English string table.
#
# Usage: scripts/update_pot.sh [--check]
#   --check  only report whether the template is out of date
#
# Strings are extracted from N_() markers in EnglishStrings.h, with "i18n:"
# comments as translator notes. Merge the result into the catalogues with
# msgmerge, or let Weblate do it.

set -euo pipefail
cd "$(dirname "$0")/.."

SRC=openrtx/include/ui/EnglishStrings.h
POT=openrtx/src/locale/OpenRTX.pot
NEW=$(mktemp)
trap 'rm -f "$NEW"' EXIT

xgettext --language=C --from-code=UTF-8 --keyword= --keyword=N_ \
         --add-comments=i18n: --add-location=file \
         --package-name=OpenRTX \
         --msgid-bugs-address=https://github.com/OpenRTX/OpenRTX/issues \
         --output="$NEW" "$SRC"

# The creation date changes on every run; ignore it when comparing
if [ -f "$POT" ] && diff -q -I '^"POT-Creation-Date:' "$POT" "$NEW" >/dev/null; then
    exit 0
fi

if [ "${1:-}" = "--check" ]; then
    echo "$POT is out of date: run scripts/update_pot.sh and commit it." >&2
    exit 1
fi

mv "$NEW" "$POT"
echo "Updated $POT"
