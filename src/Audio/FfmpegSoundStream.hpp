#pragma once

#include "Audio/SoundManager.h"

#include <filesystem>
#include <memory>
#include <string>

namespace wallpaper::audio
{

std::unique_ptr<SoundStream> CreateFfmpegSoundStream(const std::filesystem::path& media_path,
                                                     std::string* error);

} // namespace wallpaper::audio
