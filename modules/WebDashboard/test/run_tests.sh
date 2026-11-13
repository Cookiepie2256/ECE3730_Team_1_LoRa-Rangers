#!/usr/bin/env sh
# Host tests for WebDashboard (no ESP32 needed). Needs g++ and Python 3.
#   sh test/run_tests.sh
set -eu
cd "$(dirname "$0")/.."
OUT=test/build
mkdir -p "$OUT"
rm -f "$OUT"/*.json

PY=""
for c in python3 python; do
    if "$c" -c "import sys" >/dev/null 2>&1; then PY=$c; break; fi
done
[ -n "$PY" ] || { echo "Python 3 not found"; exit 1; }

"$PY" tools/embed_page.py --check
g++ -std=c++17 -Wall -Wextra -Werror -I test/stubs -I . \
    test/test_webdashboard.cpp WebDashboard.cpp -o "$OUT/test_webdashboard"
"$OUT/test_webdashboard" "$OUT"

for f in "$OUT"/*.json; do
    "$PY" -m json.tool "$f" >/dev/null || { echo "invalid JSON: $f"; exit 1; }
done
echo "JSON check: every file is valid (python -m json.tool)"
echo "All tests passed"
