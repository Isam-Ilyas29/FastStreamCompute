#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: bash scripts/generate_flamegraph.sh <input.data> <output.svg>" >&2
    exit 1
fi

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tools="$root/build/_deps/flamegraph-src"
input=$1
output=$2

mkdir -p -- "$(dirname -- "$output")"
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT

# Decode on Linux with the matching executable/debug information available.
perf script -F -period -i "$input" > "$temporary/stacks"
if ! grep -Eq '^[[:space:]]+[[:xdigit:]]+[[:space:]]' "$temporary/stacks"; then
    echo "Warning: no decoded stack frames; this graph cannot show function costs." >&2
fi
perl "$tools/stackcollapse-perf.pl" "$temporary/stacks" > "$temporary/folded"
perl "$tools/flamegraph.pl" --title="$(basename -- "$output" .svg)" \
    "$temporary/folded" > "$temporary/graph.svg"
mv -- "$temporary/graph.svg" "$output"
echo "Flame graph saved to: $output"
