#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

PROJECT_DIR="/d/projects/CipherFlow"
DATASET_DIR="/d/projects/cipherflow-benchmark-data"
WORKERS=(1 2 4 8)
REPEATS=3
EXE="$PROJECT_DIR/encrypt_decrypt.exe"

if [[ ! -f "$EXE" ]]; then echo "ERROR: executable not found: $EXE"; exit 1; fi
if [[ ! -d "$DATASET_DIR" ]]; then echo "ERROR: dataset directory not found: $DATASET_DIR"; exit 1; fi
cd "$PROJECT_DIR"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="/d/projects/benchmark-results-$RUN_ID"
LOG_DIR="$OUT_DIR/logs"
MASTER="$OUT_DIR/encrypted-master"
CSV="$OUT_DIR/results.csv"
mkdir -p "$LOG_DIR"
echo "action,repeat,workers,seconds,throughput_MB_s,verification" > "$CSV"

run_one() {
    local action="$1" repeat="$2" workers="$3" target="$4"
    local output elapsed throughput verification="N/A"
    local log_file="$LOG_DIR/${action}_r${repeat}_w${workers}.txt"
    echo "Running $action | repeat=$repeat | workers=$workers"

    if output=$("$EXE" "$target" "$action" --workers "$workers" 2>&1); then
        printf '%s\n' "$output" > "$log_file"
    else
        printf '%s\n' "$output" | tee "$log_file"
        echo "ERROR: $action failed. See $log_file"
        exit 1
    fi

    elapsed="$(printf '%s\n' "$output" | tr -d '\r' | sed -n 's/^Processing time: \([0-9][0-9.]*\) seconds.*/\1/p' | head -n 1)"
    throughput="$(printf '%s\n' "$output" | tr -d '\r' | sed -n 's/^Throughput: \([0-9][0-9.]*\) MB\/s.*/\1/p' | head -n 1)"
    if [[ -z "$elapsed" || -z "$throughput" ]]; then
        echo "ERROR: Could not parse timing/throughput. See $log_file"
        printf '%s\n' "$output"
        exit 1
    fi

    if [[ "$action" == "decrypt" ]]; then
        verification="PASS"
        local original name decrypted
        for original in "$DATASET_DIR"/*; do
            [[ -f "$original" ]] || continue
            name="$(basename "$original")"
            decrypted="$target/$name"
            if [[ ! -f "$decrypted" ]] || ! cmp -s "$original" "$decrypted"; then
                echo "  VERIFY FAIL: $name"
                verification="FAIL"
            else
                echo "  VERIFY PASS: $name"
            fi
        done
        if [[ "$verification" != "PASS" ]]; then
            echo "decrypt,$repeat,$workers,$elapsed,$throughput,$verification" >> "$CSV"
            echo "ERROR: Decrypted data mismatch. Keeping $target for inspection."
            exit 1
        fi
    fi

    echo "$action,$repeat,$workers,$elapsed,$throughput,$verification" >> "$CSV"
    echo "  time=${elapsed}s, throughput=${throughput} MB/s, verification=$verification"
}

echo "Results directory: $OUT_DIR"
echo "Dataset: $DATASET_DIR"
echo "Repeats per worker count: $REPEATS"
echo

# Benchmark encryption from fresh copies of the original dataset.
for repeat in $(seq 1 "$REPEATS"); do
    for workers in "${WORKERS[@]}"; do
        target="$OUT_DIR/work_encrypt_r${repeat}_w${workers}"
        mkdir -p "$target"
        cp -a "$DATASET_DIR/." "$target/"
        run_one encrypt "$repeat" "$workers" "$target"
        if [[ "$repeat" -eq 1 && "$workers" -eq 1 ]]; then
            mkdir -p "$MASTER"
            cp -a "$target/." "$MASTER/"
        fi
        rm -rf "$target"
    done
done

# Benchmark decryption from identical encrypted bytes on every run.
for repeat in $(seq 1 "$REPEATS"); do
    for workers in "${WORKERS[@]}"; do
        target="$OUT_DIR/work_decrypt_r${repeat}_w${workers}"
        mkdir -p "$target"
        cp -a "$MASTER/." "$target/"
        run_one decrypt "$repeat" "$workers" "$target"
        rm -rf "$target"
    done
done

median_field() {
    local action="$1" workers="$2" column="$3"
    awk -F, -v action="$action" -v workers="$workers" -v column="$column" \
        '$1 == action && $3 == workers { print $column }' "$CSV" |
        sort -n |
        awk '{ values[NR] = $1 }
             END {
                 if (NR == 0) exit 1;
                 if (NR % 2) printf "%.3f", values[(NR + 1) / 2];
                 else printf "%.3f", (values[NR / 2] + values[NR / 2 + 1]) / 2;
             }'
}

echo
echo "=== Median benchmark summary ==="
printf '%-10s %8s %12s %15s %12s %12s\n' "Action" "Workers" "Median(s)" "Median MB/s" "Speedup" "Verified"
for action in encrypt decrypt; do
    baseline="$(median_field "$action" 1 4)"
    for workers in "${WORKERS[@]}"; do
        median_time="$(median_field "$action" "$workers" 4)"
        median_throughput="$(median_field "$action" "$workers" 5)"
        speedup="$(awk -v base="$baseline" -v current="$median_time" 'BEGIN { if (current > 0) printf "%.2f", base/current; else print "N/A" }')"
        if [[ "$action" == "decrypt" ]]; then
            verified="$(awk -F, -v action="$action" -v workers="$workers" \
                '$1 == action && $3 == workers { count++; if ($6 != "PASS") failed=1 }
                 END { if (count == 0) print "N/A"; else if (failed) print "FAIL"; else print "PASS" }' "$CSV")"
        else
            verified="N/A"
        fi
        printf '%-10s %8s %12s %15s %12s %12s\n' "$action" "$workers" "$median_time" "$median_throughput" "$speedup" "$verified"
    done
done

echo
echo "Raw results CSV: $CSV"
echo "Command logs: $LOG_DIR"
echo "Encrypted reference dataset: $MASTER"
echo "Temporary test copies were removed to save disk space."
