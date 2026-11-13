#!/bin/bash
# The translations: placeholders, lists, plural forms, letters the fonts have.
set -euo pipefail
exec python3 "$(dirname "$0")/check.py" "$@"
