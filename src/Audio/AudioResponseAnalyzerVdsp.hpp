#pragma once

#include "Audio/AudioResponseService.h"

#include <cstdint>

namespace wallpaper::audio
{

void AnalyzeAudioResponseBlock(
    const float* interleaved_pcm,
    uint32_t frame_count,
    AudioSpectrumSnapshot* snapshot);

void DecayAudioResponseSnapshot(AudioSpectrumSnapshot* snapshot);

} // namespace wallpaper::audio
