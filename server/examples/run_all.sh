#!/usr/bin/env bash
set -euo pipefail

# Generate simulation outputs for all scenario files across all characters.
#
# Usage:
#   server/examples/run_all.sh [severity] [output_dir]
#
# Examples:
#   server/examples/run_all.sh
#   server/examples/run_all.sh loud
#   server/examples/run_all.sh normal /tmp/pre-buddy-sim

SEVERITY="${1:-normal}"
OUT_DIR="${2:-server/examples/output}"

case "$SEVERITY" in
  quiet|normal|loud) ;;
  *)
    echo "Invalid severity: $SEVERITY (expected: quiet|normal|loud)" >&2
    exit 2
    ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if [[ -x "$ROOT_DIR/.venv/bin/pre-buddy" ]]; then
  PRE_BUDDY="$ROOT_DIR/.venv/bin/pre-buddy"
elif command -v pre-buddy >/dev/null 2>&1; then
  PRE_BUDDY="$(command -v pre-buddy)"
else
  echo "Could not find pre-buddy CLI. Expected .venv/bin/pre-buddy or PATH." >&2
  exit 2
fi

mkdir -p "$OUT_DIR"

chars=(sage sprout sentinel)
count=0

for scenario in server/examples/*_scenario.jsonl; do
  [[ -e "$scenario" ]] || continue
  stem="$(basename "$scenario" .jsonl)"

  for ch in "${chars[@]}"; do
    base="$OUT_DIR/${stem}__${ch}__${SEVERITY}"

    "$PRE_BUDDY" simulate \
      --playback "$scenario" \
      --character "$ch" \
      --severity "$SEVERITY" \
      --format text \
      --out "${base}.txt" >/dev/null

    "$PRE_BUDDY" simulate \
      --playback "$scenario" \
      --character "$ch" \
      --severity "$SEVERITY" \
      --format csv \
      --out "${base}.csv" >/dev/null

    "$PRE_BUDDY" simulate \
      --playback "$scenario" \
      --character "$ch" \
      --severity "$SEVERITY" \
      --format json \
      --out "${base}.json" >/dev/null

    count=$((count + 1))
    echo "Generated: ${base}.{txt,csv,json}"
  done
done

echo "Done. Generated $count scenario-character bundles in: $OUT_DIR"
