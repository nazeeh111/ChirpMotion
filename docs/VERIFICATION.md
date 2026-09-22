# Verification

Tested locally with MATLAB R2026a Update 5 (26.1.0.3346908), using base MATLAB. Only MATLAB, Simulink and System Composer were installed; additional research toolboxes were not assumed available.

## Passed

Signed int16 boundary values, zero, and both signs read exactly through the facade and legacy function. All nine bundled PCM recordings returned the expected sample counts.

The 18 retained source and asset files in [SOURCE-MANIFEST.json](SOURCE-MANIFEST.json) are SHA-256 identical to the pre-rebrand snapshot. This establishes source and asset preservation, not full scientific replication. New wrappers and smoke checks are separate from those files. No computational core was rewritten.

## Reproduce

From the repository root in MATLAB:

```matlab
run('tests/smoke_test.m')
```

The test uses only local synthetic inputs or bundled data. It does not acquire or transmit signals. Assertions fail if a checked condition is not satisfied.

## Limits

Gesture recognition accuracy and the FMCW processing pipelines are unverified because Signal Processing Toolbox is not installed. No microphone, speaker output, or TCP session was started.

The facade restores the MATLAB search path after a call. Legacy figure output and computational behavior are preserved. This release has new branding, documentation, artwork, and an entry-point facade; it does not claim a new underlying research algorithm.
