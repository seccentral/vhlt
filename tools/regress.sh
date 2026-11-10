#!/usr/bin/env bash
#
# Compile reference maps and record or compare deterministic output hashes.
#
# Runs hlcsg, hlbsp, hlvis -full and hlrad -extra on every .map in --map-dir
# and hashes the resulting .bsp, .prt and entity text (ripent -export).
#
# Every tool runs with -threads 1. Threaded compiles are not deterministic:
# two runs with the same thread count can produce different .bsp and .prt
# files. Maps are compiled in parallel instead (--jobs), which uses the
# available cores while keeping each individual output stable.
#
# The compiler version and date in the BSP entity lump are replaced with a
# constant and the "compiler" key is dropped from the exported .ent before
# hashing, so hashes stay comparable across days and across version bumps
# of the same length.
#
# Maps and WADs are third-party assets and must stay outside the repository.
#
# This script and tools/regress.ps1 use the same baseline.tsv format.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/regress.sh --map-dir DIR [options]

  -m, --map-dir DIR       Directory containing the .map files (required)
  -l, --lights-rad FILE   lights.rad to pass to hlrad with -lights
  -w, --wad-dir DIR       Directory containing the WADs the maps reference;
                          the worldspawn "wad" key is rewritten to use it
  -o, --out-dir DIR       Output root (default: build/regress)
  -n, --run-name NAME     Run name (default: current timestamp)
  -u, --update-baseline   Write this run's hashes to baseline.tsv
  -j, --jobs N            Number of maps to compile at once (default: 3)
  -b, --bin DIR           Tool directory (default: build/windows/bin on
                          Windows/MSYS, build/linux/bin elsewhere)
  -h, --help              Show this help

Examples:
  tools/regress.sh --map-dir /srv/vhlt-maps \
      --lights-rad /srv/vhlt-maps/lights.rad --update-baseline
  tools/regress.sh --map-dir /srv/vhlt-maps \
      --lights-rad /srv/vhlt-maps/lights.rad
EOF
}

die() {
    echo "regress: $*" >&2
    exit 1
}

MAP_DIR=
LIGHTS_RAD=
WAD_DIR=
OUT_DIR=
RUN_NAME=
UPDATE_BASELINE=0
JOBS=3
BIN_DIR=

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m|--map-dir)       [[ $# -ge 2 ]] || die "$1 needs a value"; MAP_DIR=$2; shift 2 ;;
        -l|--lights-rad)    [[ $# -ge 2 ]] || die "$1 needs a value"; LIGHTS_RAD=$2; shift 2 ;;
        -w|--wad-dir)       [[ $# -ge 2 ]] || die "$1 needs a value"; WAD_DIR=$2; shift 2 ;;
        -o|--out-dir)       [[ $# -ge 2 ]] || die "$1 needs a value"; OUT_DIR=$2; shift 2 ;;
        -n|--run-name)      [[ $# -ge 2 ]] || die "$1 needs a value"; RUN_NAME=$2; shift 2 ;;
        -u|--update-baseline) UPDATE_BASELINE=1; shift ;;
        -j|--jobs)          [[ $# -ge 2 ]] || die "$1 needs a value"; JOBS=$2; shift 2 ;;
        -b|--bin)           [[ $# -ge 2 ]] || die "$1 needs a value"; BIN_DIR=$2; shift 2 ;;
        -h|--help)          usage; exit 0 ;;
        *)                  die "unknown argument: $1 (try --help)" ;;
    esac
done

[[ -n $MAP_DIR ]] || die "--map-dir is required"
[[ -d $MAP_DIR ]] || die "map directory not found: $MAP_DIR"
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive integer"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        EXE=.exe
        DEFAULT_BIN="build/windows/bin"
        ;;
    *)
        EXE=
        DEFAULT_BIN="build/linux/bin"
        ;;
esac

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
[[ -n $BIN_DIR ]] || BIN_DIR="$REPO_ROOT/$DEFAULT_BIN"
[[ -d $BIN_DIR ]] || die "tool directory not found: $BIN_DIR (build the preset first)"
BIN_DIR=$(cd "$BIN_DIR" && pwd)

for tool in hlcsg hlbsp hlvis hlrad ripent; do
    [[ -x "$BIN_DIR/$tool$EXE" ]] || die "missing $BIN_DIR/$tool$EXE"
done

if [[ -n $LIGHTS_RAD ]]; then
    [[ -f $LIGHTS_RAD ]] || die "lights.rad not found: $LIGHTS_RAD"
    LIGHTS_RAD=$(cd "$(dirname "$LIGHTS_RAD")" && pwd)/$(basename "$LIGHTS_RAD")
fi

if [[ -n $WAD_DIR ]]; then
    [[ -d $WAD_DIR ]] || die "wad directory not found: $WAD_DIR"
fi

[[ -n $OUT_DIR ]] || OUT_DIR="$REPO_ROOT/build/regress"
mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
[[ -n $RUN_NAME ]] || RUN_NAME=$(date +%Y%m%d-%H%M%S)
RUN_DIR="$OUT_DIR/$RUN_NAME"

shopt -s nullglob
map_files=("$MAP_DIR"/*.map)
shopt -u nullglob
[[ ${#map_files[@]} -gt 0 ]] || die "no .map files in $MAP_DIR"

mkdir -p "$RUN_DIR/work" "$RUN_DIR/results"

sha256_file() {
    sha256sum "$1" | cut -d' ' -f1
}

normalize_ent_hash() {
    sed '/^"compiler"[[:space:]]/d' "$1" | sha256sum | cut -d' ' -f1
}

normalize_bsp_hash() {
    local file=$1
    if python3 -c 'import sys' >/dev/null 2>&1; then
        python3 - "$file" <<'PY' | sha256sum | cut -d' ' -f1
import re
import sys

data = open(sys.argv[1], 'rb').read()
data = re.sub(
    rb'ZHLT v[^()]*?\([A-Z][a-z]{2} [ 0-9]{2} [0-9]{4}\)',
    rb'ZHLT vX (XXXXXXXXXXX)',
    data)
sys.stdout.buffer.write(data)
PY
    elif python -c 'import sys' >/dev/null 2>&1; then
        python - "$file" <<'PY' | sha256sum | cut -d' ' -f1
import re
import sys

data = open(sys.argv[1], 'rb').read()
data = re.sub(
    rb'ZHLT v[^()]*?\([A-Z][a-z]{2} [ 0-9]{2} [0-9]{4}\)',
    rb'ZHLT vX (XXXXXXXXXXX)',
    data)
sys.stdout.buffer.write(data)
PY
    elif perl -e 'exit 0' >/dev/null 2>&1; then
        perl -0777 -pe 's/ZHLT v[^()]*?\([A-Z][a-z]{2} [ 0-9]{2} [0-9]{4}\)/ZHLT vX (XXXXXXXXXXX)/' "$file" \
            | sha256sum | cut -d' ' -f1
    else
        echo "regress: python3, python and perl are missing; hashing the raw .bsp" >&2
        sha256_file "$file"
    fi
}

rewrite_wad_key() {
    local file=$1 wad_dir=$2
    local tmp="$file.tmp"
    WAD_DIR="$wad_dir" awk '
        BEGIN {
            dir = ENVIRON["WAD_DIR"]
            gsub(/\\/, "/", dir)
            sub(/\/+$/, "", dir)
        }
        /^"wad"[ \t]+"/ {
            line = $0
            sub(/^"wad"[ \t]+"/, "", line)
            sub(/"[ \t\r]*$/, "", line)
            n = split(line, parts, ";")
            out = ""
            for (i = 1; i <= n; i++) {
                name = parts[i]
                gsub(/\\/, "/", name)
                sub(/^.*\//, "", name)
                if (name != "") {
                    if (out != "") out = out ";"
                    out = out dir "/" name
                }
            }
            print "\"wad\" \"" out "\""
            next
        }
        { print }
    ' "$file" >"$tmp"
    mv "$tmp" "$file"
}

run_map() {
    local mapfile=$1
    local map
    map=$(basename "$mapfile" .map)
    local work="$RUN_DIR/work/$map"
    local result="$RUN_DIR/results/$map.tsv"

    rm -rf "$work"
    mkdir -p "$work"
    cp "$mapfile" "$work/$map.map"
    if [[ -n $WAD_DIR ]]; then
        rewrite_wad_key "$work/$map.map" "$WAD_DIR"
    fi
    if [[ -n $LIGHTS_RAD ]]; then
        cp "$LIGHTS_RAD" "$work/lights.rad"
    fi

    local rad_args=(-threads 1 -extra)
    if [[ -n $LIGHTS_RAD ]]; then
        rad_args+=(-lights lights.rad)
    fi

    if ! ( cd "$work" && "$BIN_DIR/hlcsg$EXE" -threads 1 "$map.map" ) >"$work/hlcsg.log" 2>&1; then
        echo "hlcsg failed" >"$work/FAILED"
        return 1
    fi
    if ! ( cd "$work" && "$BIN_DIR/hlbsp$EXE" -threads 1 "$map.map" ) >"$work/hlbsp.log" 2>&1; then
        echo "hlbsp failed" >"$work/FAILED"
        return 1
    fi
    if ! ( cd "$work" && "$BIN_DIR/hlvis$EXE" -threads 1 -full "$map.map" ) >"$work/hlvis.log" 2>&1; then
        echo "hlvis failed" >"$work/FAILED"
        return 1
    fi
    if ! ( cd "$work" && "$BIN_DIR/hlrad$EXE" "${rad_args[@]}" "$map.map" ) >"$work/hlrad.log" 2>&1; then
        echo "hlrad failed" >"$work/FAILED"
        return 1
    fi
    if ! ( cd "$work" && "$BIN_DIR/ripent$EXE" -export "$map.bsp" ) >"$work/ripent.log" 2>&1; then
        echo "ripent failed" >"$work/FAILED"
        return 1
    fi

    local bsp="$work/$map.bsp"
    local prt="$work/$map.prt"
    local ent="$work/$map.ent"
    for file in "$bsp" "$prt" "$ent"; do
        [[ -f $file ]] || { echo "missing output: $file" >"$work/FAILED"; return 1; }
    done

    {
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$map" \
            "$(sha256_file "$bsp")" \
            "$(normalize_bsp_hash "$bsp")" \
            "$(sha256_file "$prt")" \
            "$(normalize_ent_hash "$ent")" \
            "$(wc -c <"$bsp" | tr -d ' ')" \
            "$(wc -c <"$prt" | tr -d ' ')"
    } >"$result"
}

echo "Tools:    $BIN_DIR"
echo "Maps:     ${#map_files[@]} in $MAP_DIR"
echo "Output:   $RUN_DIR"

failed=0
for ((i = 0; i < ${#map_files[@]}; i += JOBS)); do
    pids=()
    for ((j = i; j < i + JOBS && j < ${#map_files[@]}; j++)); do
        run_map "${map_files[$j]}" &
        pids+=("$!")
    done
    for pid in "${pids[@]}"; do
        wait "$pid" || failed=1
    done
done

if [[ $failed -ne 0 ]]; then
    for failure in "$RUN_DIR"/work/*/FAILED; do
        [[ -f $failure ]] || continue
        map=$(basename "$(dirname "$failure")")
        echo "FAIL $map: $(cat "$failure")" >&2
        log=$(ls -t "$RUN_DIR/work/$map"/*.log 2>/dev/null | head -n 1 || true)
        if [[ -n $log ]]; then
            echo "--- $(basename "$log") ---" >&2
            tail -n 25 "$log" >&2
        fi
    done
    exit 1
fi

hashes="$RUN_DIR/hashes.tsv"
{
    echo '# vhlt-regress v1'
    printf '# generated\t%s\n' "$(date -Iseconds 2>/dev/null || date)"
    printf 'map\tbsp\tbspDateFree\tprt\tent\tbspBytes\tprtBytes\n'
    for result in "$RUN_DIR"/results/*.tsv; do
        [[ -f $result ]] && cat "$result"
    done
} >"$hashes"

baseline="$OUT_DIR/baseline.tsv"
if [[ $UPDATE_BASELINE -eq 1 ]]; then
    cp "$hashes" "$baseline"
    echo "Baseline written to $baseline"
    exit 0
fi

if [[ ! -f $baseline ]]; then
    echo "No baseline at $baseline; wrote $hashes"
    echo "Run once with --update-baseline to create the reference."
    exit 0
fi

mismatches=0
while IFS=$'\t' read -r map bsp bsp_date_free prt ent bsp_bytes prt_bytes; do
    [[ $map == \#* || -z $map ]] && continue
    actual=$(awk -F '\t' -v m="$map" '$1 == m { print $2 "\t" $3 "\t" $4 "\t" $5 }' "$hashes")
    if [[ -z $actual ]]; then
        echo "MISMATCH $map missing from run" >&2
        mismatches=$((mismatches + 1))
        continue
    fi
    IFS=$'\t' read -r actual_bsp actual_free actual_prt actual_ent <<<"$actual"
    for field in bspDateFree prt ent; do
        case "$field" in
            bspDateFree) expected=$bsp_date_free; got=$actual_free ;;
            prt)         expected=$prt;           got=$actual_prt ;;
            ent)         expected=$ent;           got=$actual_ent ;;
        esac
        if [[ $expected != "$got" ]]; then
            echo "MISMATCH $map.$field expected $expected but got $got" >&2
            mismatches=$((mismatches + 1))
        fi
    done
done <"$baseline"

if [[ $mismatches -ne 0 ]]; then
    echo "Hashes written to $hashes" >&2
    exit 1
fi

echo "Regression OK: $(($(wc -l <"$hashes") - 3)) maps match $baseline"
awk -F '\t' 'NR > 3 { printf "  %-12s bsp %s  prt %s  ent %s\n", $1, substr($3, 1, 12), substr($4, 1, 12), substr($5, 1, 12) }' "$hashes"
echo "Hashes written to $hashes"
