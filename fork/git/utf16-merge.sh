#!/bin/sh
# Git merge driver for the UTF-16 LE (BOM) files Source.cpp and miu.rc.
# Git sees them as binary and refuses to merge, so this converts all three versions
# to UTF-8, runs a normal text merge, and writes the result back as UTF-16 LE with BOM.
# Conflict markers end up in the UTF-16 file like in any other text file.
# Registered by fork/git/setup.ps1 as: sh fork/git/utf16-merge.sh %O %A %B %L %P
set -eu
base=$1 ours=$2 theirs=$3 marker=$4 path=$5

is_utf16le() { [ "$(head -c 2 "$1" | od -An -tx1 | tr -d ' \n')" = "fffe" ]; }
for f in "$base" "$ours" "$theirs"; do
  if ! is_utf16le "$f"; then
    echo "utf16-merge: $path is not UTF-16 LE with BOM on every side; merge it by hand." >&2
    exit 1
  fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
iconv -f UTF-16 -t UTF-8 "$base" > "$tmp/base"
iconv -f UTF-16 -t UTF-8 "$ours" > "$tmp/ours"
iconv -f UTF-16 -t UTF-8 "$theirs" > "$tmp/theirs"

# merge-file exits with the number of conflicts, or a value above 127 on error.
status=0
git merge-file --marker-size="$marker" -L ours -L base -L theirs "$tmp/ours" "$tmp/base" "$tmp/theirs" || status=$?
if [ "$status" -gt 127 ]; then
  echo "utf16-merge: git merge-file failed on $path." >&2
  exit 1
fi

{ printf '\377\376'; iconv -f UTF-8 -t UTF-16LE "$tmp/ours"; } > "$ours"
[ "$status" -eq 0 ]
