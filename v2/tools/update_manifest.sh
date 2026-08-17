#!/bin/sh
# Regenerates v2/traces/MANIFEST.sha256 from the promoted fixture files.
# HUMAN-RUN ONLY (promotion step 4); the loop never touches traces/.
#
# Lists every traces/*.csv, *.labels.json and *.labels.tsv as
# '<sha256>  <relative path>' sorted by path, or writes an empty manifest when
# there is nothing to promote. Verify afterwards with
#   ctest --test-dir build-host -R 'fixtures_manifest|golden'
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
traces="$here/traces"
manifest="$traces/MANIFEST.sha256"
cd "$traces"
files=$(ls -1 2>/dev/null | grep -E '\.(csv|labels\.json|labels\.tsv)$' | LC_ALL=C sort || true)
if [ -z "$files" ]; then
  : > "$manifest"
  echo "manifest: no fixtures promoted (empty MANIFEST.sha256)"
  exit 0
fi
tmp="$manifest.tmp"
: > "$tmp"
for f in $files; do
  case "$f" in
    *' '*) echo "refusing fixture name with spaces: $f" >&2; rm -f "$tmp"; exit 1 ;;
  esac
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" >> "$tmp"
  else
    sha256sum "$f" >> "$tmp"
  fi
done
mv "$tmp" "$manifest"
echo "manifest: $(wc -l < "$manifest" | tr -d ' ') file(s) listed"
