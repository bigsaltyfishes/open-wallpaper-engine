#pragma once
#include "Interface/ISceneParser.h"

#include <string>
#include <random>
#include <vector>

namespace wallpaper
{
namespace wpscene
{
struct WPUserTexture;
}

void ApplySystemUserTextures(std::vector<std::string>& textures,
                             const std::vector<wpscene::WPUserTexture>& usertextures);

class WPSceneParser : public ISceneParser {
public:
    WPSceneParser()  = default;
    ~WPSceneParser() = default;
    std::shared_ptr<Scene> Parse(const SceneParseRequest&, const std::string&, fs::VFS&, audio::SoundManager&) override;
    std::shared_ptr<Scene> Parse(std::string_view scene_id, const std::string&, fs::VFS&, audio::SoundManager&);
};
} // namespace wallpaper
