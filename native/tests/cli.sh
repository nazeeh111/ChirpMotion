#!/bin/sh
# Original CLI acceptance fixtures. Never remove user paths.
set -eu
binary=$1
case_dir=$(mktemp -d "build/cli-check.XXXXXX")
trap 'rm -rf "$case_dir"' EXIT HUP INT TERM
"$binary" --help > "$case_dir/help.txt"
"${binary%/*}/test-native" --demo "$case_dir"
input="$case_dir/align.wav"
"$binary" --input "$input" --channel 2 --output "$case_dir/valid-output" > "$case_dir/out" 2> "$case_dir/error"
test -f "$case_dir/valid-output/summary.json"
expect_refusal() {
    name=$1
    expected_status=$2
    shift 2
    destination="$case_dir/no-output-$name"
    status=0
    "$binary" --input "$input" --output "$destination" "$@" > "$case_dir/out" 2> "$case_dir/error" || status=$?
    if [ "$status" -ne "$expected_status" ]; then
        echo "FAIL invalid option $name (exit $status, expected $expected_status)" >&2; exit 1
    fi
    test ! -e "$destination"
}
expect_refusal fft-non-power 1 --channel 2 --fft-length 2047
case_number=0
for option in '--channel 0' '--channel 3' '--start-sample -1' '--start-sample 0' '--fft-length 65537' '--mode invalid' '--max-output-bytes 0' '--unknown value'; do
    case_number=$((case_number + 1))
    # Intentional splitting of these fixed, self-authored option pairs only.
    case "$option" in
        '--channel 0'|'--channel 3') expect_refusal "$case_number" 2 $option ;;
        '--max-output-bytes 0') expect_refusal "$case_number" 1 --channel 2 $option ;;
        *) expect_refusal "$case_number" 2 --channel 2 $option ;;
    esac
done
expect_refusal repeated 2 --input "$input" --channel 2
echo 'PASS actual CLI help, invalid/repeated options and no-output checks'
