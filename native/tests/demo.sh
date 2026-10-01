#!/bin/sh
set -eu
binary=$1
generator=$2
case_dir=$(mktemp -d "build/demo.XXXXXX")
"$generator" --demo "$case_dir"
"$binary" --input "$case_dir/delay.wav" --channel 2 --mode alternating --start-sample 138 --spectra-csv --output "$case_dir/delay"
"$binary" --input "$case_dir/align.wav" --channel 2 --mode alternating --output "$case_dir/align"
echo "Generated demo: $case_dir/delay/cycles.csv and $case_dir/align/summary.json"
