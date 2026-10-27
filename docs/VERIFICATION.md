# Verification

Tested locally with MATLAB R2026a Update 5 (26.1.0.3346908), using base MATLAB. Only MATLAB, Simulink and System Composer were installed; additional research toolboxes were not assumed available.

## Passed

Signed int16 boundary values, zero, and both signs read exactly through the facade and legacy function. All nine bundled PCM recordings returned the expected sample counts.

The 18-file [SOURCE-MANIFEST.json](SOURCE-MANIFEST.json) preserves the historical source/asset identities. Current source differs in the author-header comments of three FMCW scripts, updated under permission; their executable lines and line endings are unchanged. Relative to the pre-edit repository, only those three retained scripts changed. Against the historical manifest, 14 other files still match; `.gitattributes` has a pre-existing metadata change. This is source/asset evidence, not full scientific replication. New wrappers and smoke checks are separate; no computational core was rewritten.

## Reproduce

From the repository root in MATLAB:

```matlab
run('tests/smoke_test.m')
```

The test uses only local synthetic inputs or bundled data. It does not acquire or transmit signals. Assertions fail if a checked condition is not satisfied.

## Limits

Gesture recognition accuracy and the historical toolbox-dependent scripts remain unverified. The separate base-MATLAB offline analysis now runs without that toolbox. No microphone, speaker output, or TCP session was started.

The facade restores the MATLAB search path after a call. Legacy figure output and computational behavior are preserved. This release has new branding, documentation, artwork, and an entry-point facade; it does not claim a new underlying research algorithm.

## Offline extension, 2026-09-23

`tests/offline_test.m` passed in MATLAB R2026a using base MATLAB: a 300 Hz analytic beat was recovered within 1.171875 Hz in both sweep directions; known synchronization and repeated-frame invariance passed; all nine bundled filenames produced finite, nonzero spectra. Header inspection found `gesture.pcm` is stereo WAV, handled with explicit channel 2. At that recorded run, historical source and assets were unchanged. See [signal model, assumptions and evidence](OFFLINE-ANALYSIS.md). Bundled-capture processing does not establish gesture or position accuracy.
