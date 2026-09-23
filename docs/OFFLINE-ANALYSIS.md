# Offline FMCW analysis

`chirp_motion_analyze` provides a callable path from local recording or numeric samples to spectra, delays, path lengths and frame-to-frame spectral change. It does not classify gestures. The original scripts remain available without modification.

## Signal model

Defaults follow `fmcw.m` and `fmcw_dou.m`: 48 kHz samples, a 17–23 kHz linear sweep lasting 20 ms, both time endpoints included (961 samples), and a symmetric Hann envelope. `up` repeats that sweep. `alternating` interleaves up/down sweeps, producing separate output channels with their actual timing offsets. Numeric samples and raw PCM must already be mono. WAV headers are recognized even with a `.pcm` extension; multichannel WAV requires `inputChannel`.

The receiver multiplies a complete sweep by its real template and takes the magnitude of a zero-padded FFT. The largest bin in the requested beat band is reported, including DC. No high-pass or denoising filter is silently substituted for the legacy Butterworth filter. By default the band ends at 1500 Hz. Validation keeps it below both the sum-frequency component and its sampling alias, which would otherwise contaminate the real-mixer low-frequency spectrum.

For a stationary single delayed linear sweep, beat frequency is sweep slope times propagation delay. `delaySeconds = peakBeatHz / slope`; `pathLengthMeters = soundSpeed * delaySeconds`. This is total acoustic path length relative to the timing origin, not object range. Monostatic round-trip range would be half that length; separated phone transducers require known geometry. See the primary [FMCW range explanation](https://www.mathworks.com/help/phased/ug/fmcw-range-estimation.html) and [beat-to-range convention](https://www.mathworks.com/help/phased/ref/beat2range.html). The formulas do not resolve multipath, Doppler, chirp clock drift or ambiguous phone geometry.

The default 16384-point FFT has 2.9296875 Hz bin spacing. Zero padding interpolates the spectrum; it does not improve the bandwidth-limited nominal path resolution, `340/6000 = 0.0567 m`, reported separately. Spectra are unnormalized magnitudes: raw PCM keeps integer scale; WAV uses normalized amplitudes. Relative spectral change is the L2 norm of the spectral difference divided by the previous spectrum norm. It is undefined (`NaN`) in the first frame or following a zero spectrum; it is not a gesture probability.

## Synchronization and exclusions

If `startSample` is supplied, its one-based index defines the first transmitted sweep's timing origin. Otherwise the analyzer uses full linear normalized correlation to a waveform period, choosing the earliest match within 99% of the best absolute correlation. That can align to a later, stronger repetition and discard an earlier segment. The result reports `startSample`, `alignmentScore`, and discarded prefix/suffix samples. Weak auto-matches below `minAlignmentScore` (default 0.1) fail explicitly; that threshold is an input sanity check, not a calibrated detector.

Automatic alignment absorbs the strongest arrival delay. Peaks then describe delay relative to that alignment, not calibrated physical distance. Repeated chirps also make whole-period timing ambiguous. Use a known start for a controlled delay experiment. Up/down sweeps are successive, not simultaneous, so this implementation does not claim velocity cancellation or a 2D trajectory.

## Reproducible evidence

`run('tests/offline_test.m')` checks an independent analytic 1 ms delayed carrier in both sweep directions. Expected beat frequency is 300 Hz and path length 0.34 m; the measured peak error is 1.171875 Hz, within one FFT bin (path error 0.001328125 m). Identical repeated frames have exactly zero spectral change. A synthetic 137-sample leading silence is found at sample 138, with exact frame/tail counts.

All nine bundled `.pcm` filenames execute. Eight are raw recorded inputs; their spectra are finite and nonzero but have no supplied ground-truth gesture labels or distances. `gesture.pcm` is RIFF/WAVE, 48 kHz, two channels, 961000 frames, and its first channel is silent. Selecting channel 2 yields 1000 chirps and correlation approximately 1. The old byte-reader remains unchanged for compatibility and still interprets that container as raw bytes; use the new analyzer for scientific processing.

Tests also reject empty/nonfinite/too-short inputs, silent auto-alignment and unspecified stereo-channel selection. No microphone, speaker, TCP session, gesture classifier, or historical toolbox pipeline is run.
