#include "Runtime/SceneRuntimeContext.hpp"
#include "WPSoundParser.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace wallpaper
{
namespace
{

class FakeSoundStream final : public audio::SoundStream {
public:
    uint64_t NextPcmData(void*, uint32_t) override { return 0; }
    void     PassDesc(const Desc&) override {}
};

TEST(SceneScriptSoundLayerSmoke, ScriptControlsNativeSoundLayer) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {
        .canvas_width  = 3840,
        .canvas_height = 2160,
    });
    ASSERT_NE(runtime, nullptr);

    WPSoundStream::StreamFactory factory = [](const audio::SoundStream::Desc&) {
        return std::make_shared<FakeSoundStream>();
    };
    auto sound_layer = std::make_shared<WPSoundStream>(
        std::vector<WPSoundStream::StreamFactory> { std::move(factory) },
        WPSoundStream::Config { .startsilent = true });

    runtime->RegisterSoundLayer("morningAudio", sound_layer);
    ASSERT_TRUE(runtime->HasSoundLayer("morningAudio"));
    ASSERT_FALSE(runtime->SoundLayerPlaying("morningAudio"));

    runtime->RegisterSceneScript(
        R"JS(
function update() {
  var morningAudio = scene.getObject('morningAudio');
  if (!morningAudio.isPlaying()) {
    morningAudio.play();
  }
}
)JS",
        "");
    ASSERT_EQ(runtime->sceneScriptCount(), 1u);

    runtime->Tick(1.0 / 60.0);

    EXPECT_TRUE(runtime->SoundLayerPlaying("morningAudio"));
    EXPECT_EQ(runtime->scriptErrorCount(), 0u);
}

} // namespace
} // namespace wallpaper
