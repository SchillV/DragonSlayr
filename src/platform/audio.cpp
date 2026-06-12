#include "platform/audio.hpp"

#include "core/log.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ds {

namespace {

constexpr uint32_t kRate = 48000;

// freq sweeps f0 -> f1 with exponential decay; noise_mix adds grit.
std::vector<int16_t> synth_blip(float f0, float f1, float dur_s, float volume, float noise_mix) {
    const auto frames = static_cast<size_t>(dur_s * kRate);
    std::vector<int16_t> pcm(frames);
    float phase = 0.0f;
    uint32_t noise_state = 0x12345u;
    for (size_t i = 0; i < frames; ++i) {
        const float t01 = static_cast<float>(i) / static_cast<float>(frames);
        const float freq = f0 + (f1 - f0) * t01;
        phase += freq * (2.0f * 3.14159265f / kRate);
        noise_state = noise_state * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(noise_state >> 16) / 32768.0f) - 1.0f;
        const float env = std::exp(-4.0f * t01);
        const float s = (std::sin(phase) * (1.0f - noise_mix) + noise * noise_mix) * env * volume;
        pcm[i] = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
    }
    return pcm;
}

std::vector<int16_t> synth_arpeggio(float volume) {
    std::vector<int16_t> out;
    for (const float f : {330.0f, 415.0f, 495.0f}) {
        const auto seg = synth_blip(f, f, 0.08f, volume, 0.05f);
        out.insert(out.end(), seg.begin(), seg.end());
    }
    return out;
}

struct Voice {
    ma_audio_buffer buffer{};
    ma_sound sound{};
    bool sound_ok = false;
    bool buffer_ok = false;
};

struct Effect {
    std::vector<int16_t> pcm; // empty when voices stream from a wav file
    std::array<Voice, 4> voices;
    int next = 0;
};

} // namespace

struct Audio::Impl {
    ma_engine engine{};
    bool engine_ok = false;
    std::map<std::string, Effect, std::less<>> effects;
};

Audio::~Audio() {
    shutdown();
}

bool Audio::init(const std::filesystem::path& sounds_dir) {
    impl_ = new Impl();
    if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS) {
        log_warn("audio device unavailable; continuing silent");
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->engine_ok = true;

    const struct {
        const char* name;
        std::vector<int16_t> pcm;
    } defs[] = {
        {"swing", synth_blip(200.0f, 90.0f, 0.18f, 0.30f, 0.40f)},
        {"bolt_fire", synth_blip(760.0f, 320.0f, 0.22f, 0.28f, 0.10f)},
        {"hit", synth_blip(150.0f, 110.0f, 0.12f, 0.35f, 0.50f)},
        {"hurt", synth_blip(110.0f, 70.0f, 0.30f, 0.40f, 0.30f)},
        {"kill", synth_arpeggio(0.30f)},
        {"dash", synth_blip(500.0f, 900.0f, 0.15f, 0.18f, 0.65f)},
    };

    for (auto& def : defs) {
        Effect& effect = impl_->effects[def.name];
        const std::filesystem::path wav = sounds_dir / (std::string(def.name) + ".wav");
        std::error_code ec;
        const bool use_file = std::filesystem::exists(wav, ec);
        if (!use_file) {
            effect.pcm = def.pcm;
        }
        for (Voice& v : effect.voices) {
            if (use_file) {
                if (ma_sound_init_from_file(&impl_->engine, wav.string().c_str(),
                                            MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                            &v.sound) == MA_SUCCESS) {
                    v.sound_ok = true;
                }
            } else {
                ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
                    ma_format_s16, 1, effect.pcm.size(), effect.pcm.data(), nullptr);
                cfg.sampleRate = kRate;
                if (ma_audio_buffer_init(&cfg, &v.buffer) == MA_SUCCESS) {
                    v.buffer_ok = true;
                    if (ma_sound_init_from_data_source(&impl_->engine, &v.buffer, 0, nullptr,
                                                       &v.sound) == MA_SUCCESS) {
                        v.sound_ok = true;
                    }
                }
            }
        }
    }
    log_info("audio initialized ({} effects)", impl_->effects.size());
    return true;
}

void Audio::shutdown() {
    if (!impl_) {
        return;
    }
    for (auto& [name, effect] : impl_->effects) {
        for (Voice& v : effect.voices) {
            if (v.sound_ok) {
                ma_sound_uninit(&v.sound);
            }
            if (v.buffer_ok) {
                ma_audio_buffer_uninit(&v.buffer);
            }
        }
    }
    if (impl_->engine_ok) {
        ma_engine_uninit(&impl_->engine);
    }
    delete impl_;
    impl_ = nullptr;
}

void Audio::play(std::string_view name) {
    if (!impl_) {
        return;
    }
    const auto it = impl_->effects.find(name);
    if (it == impl_->effects.end()) {
        return;
    }
    Effect& effect = it->second;
    Voice& v = effect.voices[static_cast<size_t>(effect.next)];
    effect.next = (effect.next + 1) % static_cast<int>(effect.voices.size());
    if (!v.sound_ok) {
        return;
    }
    ma_sound_seek_to_pcm_frame(&v.sound, 0);
    ma_sound_start(&v.sound);
}

} // namespace ds
