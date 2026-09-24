Native FMOD 2.02.13 compressor captures, 48 kHz stereo float32 little endian, 8192 frames each. Threshold -10 dB, ratio 3, release 250 ms, makeup 5 dB. Generated with web/tools/native_compressor_probe.cpp; buffer 128 frames.

- burst: attack 180 ms, default asymmetric burst/impulse input.
- step1/step20/step180: constant stereo 1.0, indicated attack in ms.
- mono: attack 20 ms, left 1.0, right 0.0.
- unequal: attack 20 ms, left 1.0, right 0.5.

Capture commands: native_compressor_probe 0 OUTPUT 128 ATTACK [LEFT [RIGHT]]. Trim the capture to the first 65536 bytes. The test checks every sample both through the detector kernel and decoding/channel/DSP mixer path, accounting for the existing two-frame mixer startup latency. Tolerance 0.00003 records the observed Wasm floating-point difference (largest error 0.00002432 on the burst); this is not bit-identical audio.

Group ordering fixtures: group-pre/group-post use attack 20 ms, stereo amplitude 1, group volume 0.25 and DSP indices 1/0 respectively. Native group volume ramp is explicitly disabled to isolate the graph ordering. Commands append `0.25 1` or `0.25 0` to the native probe's left/right amplitude arguments. Browser group tests align three startup frames (channel, SFX group, master); the new gain-only fader adds no additional delay. Native default volume ramp remains separate coverage to implement.

channel-pre/channel-post repeat the group ordering experiment on a Channel at volume 0.25, with native volume ramp disabled. Append `0.25 1 0 1` or `0.25 0 0 1` after left/right input amplitudes. The last flag selects channel gain/effect insertion. Every sample is checked through actual channel routing in addition to the compressor kernel.
