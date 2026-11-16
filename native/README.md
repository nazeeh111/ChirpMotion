# ChirpMotion native analyzer

Stream acoustic chirp recordings into cycle measurements and optional spectra on macOS or Linux. The program uses C17, the system math library and seekable files. It acquires no audio and makes no remote calls.

Current development source identifies as `0.1.1-dev.0` with `build/chirpmotion-native --version`. The earlier `native-v0.1.0` release package and retained output records remain historical; the output format remains `chirpmotion.native/v1`.

```sh
make check demo
```

Run this command in `native/`, or use `make -C native check demo` from the repository root. `check` includes an independent direct Fourier transform, generated beat/timing/alignment cases, parser refusals, output preservation and a 512-input deterministic parser corpus. `demo` creates a new directory under `build/` each time. All demo signals are newly generated; no inherited recordings are included.

The known-start demo emits six cycles. Its first two sweep rows are:

| Sweep | Start (seconds) | Peak (Hz) | Relative path (meters) |
| --- | ---: | ---: | ---: |
| Up | 0.0028541667 | 298.828125 | 0.338671875 |
| Down | 0.022875 | 298.828125 | 0.338671875 |

The expected analytic beat is 300 Hz. The 1.171875 Hz difference is below the default bin spacing; it is not a physical distance-accuracy measurement. The separate alignment demo reports start sample 138 and 19 discarded tail samples.

```sh
build/chirpmotion-native --input recording.wav --channel 2 \
  --mode alternating --output new-analysis
```

Use `--mode up` for repeated upward sweeps. `--start-sample 138` supplies a known one-based timing origin. Without it, the program finds the earliest match within 99% of the global best absolute normalized correlation. Two bounded correlation scans locate that start; a third pass streams measurements. The comparison permits an absolute score tolerance of `1e-12`. Inputs must remain unchanged during processing.

Paths are relative to the working directory. The input must be seekable, and the new output directory's parent must exist. Completion prints the actual result paths. Read `summary.json` for run settings, then `cycles.csv` for each sweep's `quality`; optional `spectra.csv` contains the supporting raw magnitudes.

| Exit | Meaning |
| --- | --- |
| 0 | Complete files were written. `ambiguous` and `no_signal` are valid row outcomes and have `null` paths. |
| 1 | Input, analysis or output failed. Read the error and any `INCOMPLETE.txt`; use a fresh destination to retry. |
| 2 | A required option is missing, repeated, unknown or syntactically invalid. |

An exit of 0 does not establish a usable physical measurement. FFT and output-limit values rejected by analysis, rather than the option parser, exit 1.

## Files and output

The input profile is mono raw little-endian signed int16 PCM, or classic RIFF/WAVE PCM16 at **48 kHz**, with one or two channels. Stereo requires `--channel 1` or `--channel 2`. WAVE headers are detected regardless of extension; raw PCM keeps integer amplitude units, while WAVE samples are divided by 32768. Unknown WAVE chunks, including odd-length padded chunks, are skipped. Compressed, extensible, floating-point, RF64 and big-endian WAVE are refused, as are inconsistent or truncated chunks. At most 128 chunks and 1 GiB input are supported.

`--fft-length N` accepts powers of two from 1024 to 65536; the default is 16384. `--spectra-csv` adds full beat-band spectra. Compact cycle output is the default for long recordings. `--max-output-bytes N` can lower the total CSV/JSON cap from its 512 MiB default. Values outside these bounds and repeated/unknown options fail.

The destination must be a new directory with an existing parent. Results are:

- `cycles.csv`: zero-based cycle and sweep indices, sweep start time, raw peak beat frequency, relative delay/path, relative spectral change, quality and bounded peak/noise evidence.
- `summary.json`: format `chirpmotion.native/v1`, input conventions, selected channel, options, sample counts, alignment score, discarded prefix/suffix and peak owned processing heap.
- `spectra.csv`, when requested: cycle/sweep indices, frequency and unnormalized magnitude.

Undefined numeric values use `null` in JSON and CSV. An explicit start has no alignment score. A zero spectrum has no peak; spectral change is undefined for the first cycle or after a zero previous spectrum. Files are written as `.partial` and the completed summary is finalized last. A failed write, limit or close leaves an owned directory marked `INCOMPLETE.txt`, without a completed summary. Existing destinations are never replaced.

## Measurements and timing

The declared waveform matches the existing offline model: a 17–23 kHz linear sweep lasting 20 ms, both endpoints included, hence **961 samples**, with a symmetric Hann envelope. Alternating mode has an upward and then downward sweep, a 1922-sample cycle. The downward sweep starts `961/48000` seconds after its upward sweep. Multiplication by the corresponding real template precedes the FFT; there is no added high-pass filter or normalization.

The largest bin from DC through 1500 Hz is reported, with the lowest index winning ties. Each sweep has a heuristic `quality`: `no_signal` only for an exactly zero spectrum; `ambiguous` if the peak is within 150 Hz of a band boundary, or a bin outside the peak's ±150 Hz neighborhood reaches 65% of the peak, or peak magnitude is less than four times the RMS magnitude outside that neighborhood. A second local maximum anywhere in the reported band, including a one-sided endpoint maximum, also makes the result ambiguous when it reaches 65% of the dominant peak and the lowest intervening magnitude is at most 50% of that second peak. Otherwise the result is `usable`. The neighborhood is rounded up to FFT bins. These fixed thresholds are an uncalibrated screening policy, not a probability, measured signal-to-noise ratio, or guarantee that a beat corresponds to a physical path. A sufficiently narrow noise peak or unresolved nearby components can pass. Raw peak and optional spectra remain available, but delay and path are `null` for `no_signal` and `ambiguous`. The peak and competing magnitude, outside-neighborhood RMS magnitude and their peak-to-RMS ratio are included in each row. The strongest qualifying resolved competitor and its intervening valley are also reported; the competitor is zero and the valley `null` when none qualifies. `null` denotes an undefined ratio. Default bin spacing is `48000/16384 = 2.9296875 Hz`. Derived delay is `peak_hz/300000`; multiplying by 340 m/s gives relative total acoustic path length. Automatic alignment absorbs arrival delay. These quantities are not calibrated object distances, recognized gestures or a simultaneous velocity estimate. Zero padding does not improve bandwidth-limited physical resolution. [The existing model explanation](https://github.com/nazeeh111/ChirpMotion/blob/cfdaae5f01ce0f9daf4354579c33fdaba11fd542/docs/OFFLINE-ANALYSIS.md) records those distinctions and primary references.

Owned buffers depend on FFT size and waveform period, not recording length. They retain one transform, current/previous spectra and bounded correlation blocks. The default processing heap is below 4 MiB; a 65536-point transform stays below 2 MiB of owned processing heap. These bounds exclude system I/O buffers and process/runtime overhead. `./build/test-native --memory` executes the default configuration on generated 1 MiB and 64 MiB files and compares allocation peaks. This is a memory check, with no claim of faster execution.

## Development and provenance

```sh
make CC=clang check
python3 tests/test_quality.py build/chirpmotion-native
make sanitize
```

CI runs GCC and Clang on Ubuntu, including sanitizer and fixed-buffer checks. The MATLAB workflows are verified separately.

The native implementation, tests and generated signals are covered by [MIT](LICENSE). A native-only package contains the files listed in [RELEASE-MANIFEST.txt](RELEASE-MANIFEST.txt), excluding inherited scripts, recordings and built binaries.
