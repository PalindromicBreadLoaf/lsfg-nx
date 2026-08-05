#!/usr/bin/env bash
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

# Every file this repository authors carries a copyright line and an SPDX
# identifier.

set -euo pipefail

cd "$(dirname "$0")/.."

missing=0

while IFS= read -r file; do
    if ! head -n 5 "$file" | grep -q "SPDX-License-Identifier: GPL-3.0-or-later"; then
        echo "missing SPDX identifier: $file"
        missing=1
    fi

    if ! head -n 5 "$file" | grep -q "Copyright 2026 PalindromicBreadLoaf"; then
        echo "missing copyright line: $file"
        missing=1
    fi
done < <(
    find . \
        \( -path ./build -o -path ./.git -o -path ./third_party \) -prune -o \
        -type f \
        \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
           -o -name '*.s' -o -name '*.S' -o -name '*.sh' -o -name '*.cmake' \
           -o -name '*.specs' -o -name '*.ini' -o -name '*.yml' -o -name '*.in' \) \
        -print
)

if [ "$missing" -ne 0 ]; then
    echo
    echo "Add"
    echo " // Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)"
    echo " // SPDX-License-Identifier: GPL-3.0-or-later"
    exit 1
fi

echo "All source files carry the license header."
