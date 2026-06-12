#!/bin/sh
#
# Run cppcheck on all source files.
#
# Usage: run-cppcheck.sh [builddir]
#   Uses pkg-config from <builddir>/build.ninja to gather include flags.

set -e

BUILDDIR="${1:-build}"

if [ ! -d "$BUILDDIR" ]; then
  echo "error: build directory '$BUILDDIR' not found."
  exit 1
fi

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$(cd "$SELF_DIR/.." && pwd)"

# Gather include flags from the Meson build's compile commands.
INCLUDES=$(python3 -c "
import json
with open('$BUILDDIR/compile_commands.json') as f:
    data = json.load(f)
for entry in data:
    if '../src/stamp-application.c' in entry.get('file', ''):
        args = ' '.join(entry.get('arguments', []))
        for a in args.split():
            if a.startswith('-I') or a.startswith('-D'):
                print(a)
        break
")

SRC_FILES=$(find "$SRCDIR/src" -name "*.c" -not -path "*/meson*" | sort)

exec cppcheck \
  --enable=warning,style,performance,portability \
  --suppress=unusedFunction \
  --suppress=missingIncludeSystem \
  --suppress=constParameter \
  --suppress=constParameterCallback \
  --suppress=constVariablePointer \
  --suppress=constParameterPointer \
  --suppress=nullPointerRedundantCheck \
  --suppress=unmatchedSuppression \
  --suppress=unknownMacro \
  --error-exitcode=1 \
  --quiet \
  --std=c11 \
  $INCLUDES \
  $SRC_FILES
