#include "miniaudio.h"
#include <emscripten.h>
#include <cmath>
#include <cstdio>

int main() {
    const char* paths[] = {"/audio/PistolFire.flac", "/audio/uwinfinal.ogg"};
    for (const auto* path : paths) {
        ma_decoder decoder;
        auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
        auto result = ma_decoder_init_file(path, &config, &decoder);
        if (result != MA_SUCCESS) { std::printf("FAIL decode %s: %d\n", path, result); return 1; }
        ma_uint64 total = 0, count = 0;
        float samples[4096]; double energy = 0;
        do {
            result = ma_decoder_read_pcm_frames(&decoder, samples, 2048, &count);
            total += count;
            for (ma_uint64 i=0; i<count*2; ++i) {
                if (!std::isfinite(samples[i])) { ma_decoder_uninit(&decoder); return 2; }
                energy += samples[i]*samples[i];
            }
        } while (result == MA_SUCCESS && count);
        ma_decoder_uninit(&decoder);
        if (!total || energy == 0 || result != MA_AT_END) { std::printf("FAIL empty/corrupt audio %s: %d\n", path, result); return 3; }
        std::printf("Decoded real asset %s: %llu stereo frames, energy %.3f\n", path, total, energy);
    }
    if (emscripten_run_script_int("typeof window !== 'undefined'")) {
        ma_engine engine;
        auto config = ma_engine_config_init();
        config.sampleRate = 48000;
        if (ma_engine_init(&config, &engine) != MA_SUCCESS) { std::puts("FAIL browser output initialization"); return 4; }
        if (ma_engine_play_sound(&engine, paths[0], nullptr) != MA_SUCCESS) { ma_engine_uninit(&engine); return 5; }
        // The browser's audio device can take more than a second to start on a slow
        // machine (CI once rendered nothing in 1.5 s): wait up to 10 s for it to start,
        // then require it to keep rendering.
        ma_uint64 started = 0;
        for (int waited = 0; waited < 10000 && (started = ma_engine_get_time_in_pcm_frames(&engine)) == 0; waited += 100) {
            emscripten_sleep(100);
        }
        emscripten_sleep(500);
        auto frames = ma_engine_get_time_in_pcm_frames(&engine);
        std::printf("Browser audio rendered %llu frames, then %llu half a second later.\n", started, frames);
        ma_engine_uninit(&engine);
        if (started == 0 || frames <= started) return 6;
    }
    std::puts("Audio decoder/output check passed.");
    return 0;
}
