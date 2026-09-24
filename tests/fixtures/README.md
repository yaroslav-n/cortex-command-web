# Native audio reference

`native_lowpass.hpp` contains the first 128 left-channel samples from a stereo unit impulse through the repository's bundled FMOD 2.02.13 multiband EQ. The mixer runs at 48,000 Hz with 512-frame buffers, using `FMOD_OUTPUTTYPE_NOSOUND_NRT`. Band A uses its native default filter LOWPASS_12DB and Q 0.707; only its cutoff changes (400, 8,000, 22,000 Hz). Captures include filter output before master processing. No normalization or alignment was applied.

`tools/native_filter_probe.cpp` generates the raw interleaved float32 recordings. `tools/native_dsp_defaults.cpp` queries the library's actual default parameters. Build either from the project root with the Command Line Tools compiler, the include path `original-code/external/include/fmod-2.2.13`, and library path `original-code/external/lib/macos` (`-lfmod`, with that directory also in the executable rpath). The filter probe accepts a cutoff and output filename.

`audio_contract` compares the production filter against these independent native recordings, with an absolute per-sample tolerance of 0.000002, and checks that channel histories remain independent. These impulse tests establish fixed-cutoff filter agreement; they do not establish complete mixer, dynamic cutoff, compressor, limiter, spatial audio, or perceptual equivalence.

Measured native DSP defaults:

- Multiband EQ: band A 12 dB lowpass, 8,000 Hz, Q 0.707, gain 0; bands B–E disabled.
- Compressor: threshold 0 dB, ratio 2.5, attack 20 ms, release 200 ms, makeup 0 dB. The game explicitly overrides all five values.
- Limiter: release 10 ms, ceiling 0 dB, maximizer gain 0 dB. The game uses its defaults. The browser implementation still needs correction and independent waveform verification.

The integration test also renders the fixtures through WAV decoding and the complete production mixer graph. It verifies both channels after the graph's measured two-frame startup latency (about 41.7 microseconds at 48 kHz). This latency remains a known timing difference. Kernel comparison is unshifted; mixer comparison uses the explicitly asserted two-frame offset. Lifecycle/silence checks use a fresh graph, isolating them from filter tails.


## Limiter native references

`limiter/{10,100,ceiling,linked}.f32` contains 8192 stereo float32 frames from the bundled native FMOD library at 48 kHz. `tools/native_limiter_probe.cpp` captures them. Input is constant (0.25, 0.125), with left=4 on frames 256..383, left=-2 at 1536, and right=2 on 3072..3327. Release fixtures use 10 or 100 ms; ceiling uses release=10 ms, ceiling=-6 dB, maximizer=6 dB; linked uses native stereo linking enabled. Other parameters retain native defaults.

Measured native behavior: immediate attack, default independent channel limiting, multiplicative gain recovery, ceiling scaling, maximizer gain, and sequential channel processing in linked mode. Default linked mode is false. The browser kernel matches default, ceiling and linked fixtures within 4.2e-6 absolute PCM error; the 100 ms release fixture differs by up to 5.19e-5, an explicitly remaining numerical difference. Tests use 1e-5 tolerance for 10 ms fixtures and 6e-5 for 100 ms. These are measured tolerances, not bit-exact equivalence claims.

The default limiter is also tested through actual WAV decoding and the production graph, including its final sample. That integration test found the pitch resampler lost the last decoded frame at EOF. CountedSource now supplies one final zero lookahead frame, only at final EOF and never between loops, so the last real sample reaches output. The graph comparison retains the previously measured two-frame startup offset. Existing lowpass, loop, callback, fade and teardown checks still pass.

## Compressor investigation

`compressor-baseline.f32` is the same 8192-frame input through native compressor settings actually used by AudioMan: threshold=-10 dB, ratio=3, attack=180 ms, release=250 ms, makeup=5 dB. Reproduce with `tools/native_compressor_probe.cpp 0 output.f32`. This is a baseline for further work, not a passing browser equivalence fixture. A calculation using the browser's current envelope formula differs by up to 1.09608 in absolute PCM over this input. Native compression first affects the quiet post-burst samples at frame 792. The current browser detector/dynamics therefore still require replacement and verification.
