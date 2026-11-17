# Follow the chirp, then inspect the decision

The native C17 analyzer streams real-template multiplication, FFT magnitudes and
per-sweep screening. This comparison uses the **existing generated fixtures**
`single` and `two` in `native/tests/test_quality.py` from source
`db81f1af36f09498c8b3eaeb4368e93347420225` (tree
`1193e9fac8e7e51afe0e4d7dafed1f1311b2db85`). No microphone, retained recording,
MATLAB session, network service or new estimator was used to produce these cases.

## Read the two outcomes

- **One component:** a generated chirp carrier evaluated at `t − 1 ms`, with its Hann envelope fixed to `t`, has an analytic 300 Hz beat.
  Native output reports **298.828125 Hz**, `quality=usable`, delay
  **0.00099609375000000002 s**, and modeled relative total acoustic path
  **0.33867187500000001 m**. The expected model path is 0.34 m.
- **Two components:** generated 300 Hz and 850 Hz beat components produce a
  dominant **849.609375 Hz** bin and `quality=ambiguous`. Raw spectral evidence
  remains available; **delay and path are `null`**. The competing magnitude is
  539879.94996983488 versus dominant 539986.37788560276, with a resolved intervening
  valley. See the complete row for all screening evidence.

Both commands returned **0**: the files completed, including the screened-out
case. A `usable` row is an uncalibrated heuristic outcome, not a confidence level
or proof of a physical path. A narrow noise peak or unresolved components can
still pass. See the [complete current screening rules](../../native/README.md#measurements-and-timing).

## Run the exact source path

A C compiler, make and Python 3's standard library are enough for the new comparison.
From the complete checkout, choose a new destination whose parent exists:

```sh
make -C native check
python3 native/tests/test_quality.py native/build/chirpmotion-native
python3 docs/presentation/run-evidence.py native/build/chirpmotion-native new-chirp-evidence
```

The helper uses the existing test fixture function unchanged and invokes the real
CLI twice. The equivalent command for the single-component generated input is:

```sh
native/build/chirpmotion-native --input new-chirp-evidence/single/input.pcm \
  --start-sample 1 --spectra-csv --output another-fresh-analysis
```

`summary.json` records 48 kHz, 961 samples, explicit start sample 1 and one upward
sweep. The default 16384-point FFT gives **2.9296875 Hz** bin spacing. The fixture
uses a 17–23 kHz linear carrier with a symmetric Hann envelope. Raw PCM retains
signed int16 amplitude units. One component uses amplitude 12000; the two-component
fixture uses amplitude 6000 per component. Spectra are **unnormalized**, so the
panels preserve that different input construction rather than rescaling peaks.

The timing origin is supplied, not estimated in these two cases. The existing
`make -C native demo` separately checks alternating sweeps and automatic alignment:
our executed demo retained six cycles, start sample 138, and 19 discarded suffix
samples in its alignment example. The existing MATLAB workflow remains available
and is not represented here as rerun.

## Inspect or reopen without running analysis

[Download the evidence ZIP](chirp-screening.zip) and unpack it into a fresh directory.
It contains `manifest.json`, `README.md`, and each case's `input.pcm`, `summary.json`,
`cycles.csv` and `spectra.csv`. Every file named by the manifest is included, with
its byte count and SHA256. The bundle is data for inspection; regenerating outputs
requires the complete pinned source checkout and C compiler, external to the ZIP.

The bundle was actually extracted into a fresh directory, all declared paths and
hashes checked, and its native summary/CSV files reopened. Equivalent checks:

```python
import csv, hashlib, json
from pathlib import Path
root = Path("unpacked-evidence")
manifest = json.loads((root / "manifest.json").read_text())
for name, expected in manifest["files"].items():
    raw = (root / name).read_bytes()
    assert len(raw) == expected["bytes"]
    assert hashlib.sha256(raw).hexdigest() == expected["sha256"]
with (root / "two/cycles.csv").open() as stream:
    row = next(csv.DictReader(stream))
assert row["quality"] == "ambiguous"
assert row["relative_path_meters"] == "null"
```

No archive reports, plots or expected files depend on a hidden sibling directory.
The manifest's `source_external_url` and `fixture_source` identify the external
implementation, not missing bundle members.

## Figure and physical meaning

The [editable renderer](render.py) reads the preserved data and uses existing NumPy
and Matplotlib: `python docs/presentation/render.py`. It runs no analysis. Both
figures show **all 961 PCM samples** and **all 513 stored 0–1500 Hz spectrum bins**
per case. Time is sample index / 48000 × 1000 for milliseconds. There is no new
filter, fit, normalization, smoothing, exclusion or path simplification. The two
spectra share limits; peak markers are the exact native values. The [figure manifest](figure-manifest.json)
binds source arrays, transforms and exports. Both [SVG](screening.svg) and [portrait SVG](screening-mobile.svg)
retain text with portable local font fallbacks; PNG counterparts and a [standalone HTML view](screening.html)
are included. Plot bytes can vary by renderer/runtime; scientific reproduction
means checking the preserved inputs and numeric outputs, not assuming identical
PNG bytes across versions.

The implementation divides beat frequency by a 300000 Hz/s slope, then multiplies
by an assumed 340 m/s sound speed. It reports relative **total acoustic path**.
It is not calibrated object distance; transducer geometry, multipath, Doppler,
clock drift and physical validation are not solved here. Automatic alignment can
absorb arrival delay. Zero padding does not improve bandwidth-limited physical
resolution. No confidence interval is estimated in this comparison.

See [the full signal model and MATLAB method](../OFFLINE-ANALYSIS.md),
[native limits](../../native/README.md), [verification history](../VERIFICATION.md),
[original native MIT license](../../native/LICENSE), and [repository scope](../../LICENSE).
Retained recordings and adapted research scripts keep their existing terms.
