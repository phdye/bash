#!/bin/sh
# md2man.sh -- convert a markdown man page to roff via pandoc
# Usage: md2man.sh INPUT.md OUTPUT
#
# Extracts TITLE(SECTION) from the first heading line, generates a YAML
# front-matter block, and pipes the result through pandoc -s -t man.

set -e

if [ $# -ne 2 ]; then
    echo "usage: md2man.sh INPUT.md OUTPUT" >&2
    exit 1
fi

INPUT="$1"
OUTPUT="$2"

# Read first line; expect: # TITLE(N) -- description
# Handles: backslash-escaped underscores, -- or em-dash separators
first=$(head -1 "$INPUT")

# Strip leading "# ", collapse \_ to _
clean=$(echo "$first" | sed 's/^# //; s/\\_/_/g')

# Extract TITLE and SECTION from TITLE(N)
TITLE=$(echo "$clean" | sed 's/(.*//' | sed 's/[[:space:]]*$//')
SECTION=$(echo "$clean" | sed 's/[^(]*(\([0-9]\)).*/\1/')

# Description: everything after -- or em-dash
DESC=$(echo "$clean" | sed 's/^[^-—]*[--—][[:space:]]*//')

# Section-specific header text
case "$SECTION" in
    1) HEADER="bash-server commands" ;;
    3) HEADER="bash-server library" ;;
    5) HEADER="bash-server file formats" ;;
    7) HEADER="bash-server concepts" ;;
    *) HEADER="bash-server" ;;
esac

DATE=$(date +%Y-%m-%d)

# Build output directory if needed
outdir=$(dirname "$OUTPUT")
[ -d "$outdir" ] || mkdir -p "$outdir"

# Pipe YAML front-matter + rest of file (skip line 1) into pandoc
{
    cat <<EOF
---
title: $TITLE
section: $SECTION
header: $HEADER
footer: bash-server 0.1.0
date: $DATE
---
EOF
    # Emit the rest of the file starting from line 2
    tail -n +2 "$INPUT"
} | pandoc -s -t man -o "$OUTPUT"
