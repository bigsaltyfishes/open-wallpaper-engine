#include "Runtime/SceneRuntimeContext.hpp"
#include "Scene/SceneNode.h"

#include <gtest/gtest.h>

#include <memory>

namespace wallpaper
{
namespace
{

struct RuntimeWithProbeNodes {
    std::shared_ptr<SceneNode>           exported_probe;
    std::shared_ptr<SceneNode>           callback_probe;
    std::unique_ptr<SceneRuntimeContext> runtime;
};

RuntimeWithProbeNodes CreateRuntimeWithProbeNodes() {
    RuntimeWithProbeNodes fixture;
    fixture.runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {
        .canvas_width  = 3840,
        .canvas_height = 2160,
    });
    if (fixture.runtime == nullptr) return fixture;

    fixture.exported_probe = std::make_shared<SceneNode>();
    fixture.callback_probe = std::make_shared<SceneNode>();
    fixture.exported_probe->SetVisible(false);
    fixture.callback_probe->SetVisible(false);

    fixture.runtime->RegisterNode("exportedProbe", fixture.exported_probe.get());
    fixture.runtime->RegisterNode("callbackProbe", fixture.callback_probe.get());
    return fixture;
}

TEST(SceneScriptMediaEventSmoke, ExportedHandlerAndSceneCallbackReceivePlaybackEvent) {
    auto fixture = CreateRuntimeWithProbeNodes();
    ASSERT_NE(fixture.runtime, nullptr);
    ASSERT_FALSE(fixture.runtime->NodeVisible("exportedProbe"));
    ASSERT_FALSE(fixture.runtime->NodeVisible("callbackProbe"));

    fixture.runtime->RegisterSceneScript(
        R"JS(
function mediaPlaybackChanged(event) {
  if (event.state === 0) {
    scene.getObject('exportedProbe').visible = true;
  }
}

scene.on('mediaPlaybackChanged', function(event) {
  if (event.state === 0) {
    scene.getObject('callbackProbe').visible = true;
  }
});
)JS",
        "");
    ASSERT_EQ(fixture.runtime->sceneScriptCount(), 1u);

    fixture.runtime->SetMediaIntegrationEnabled(true);
    fixture.runtime->DispatchMediaEventJson(R"({"type":"mediaPlaybackChanged","state":0})");

    EXPECT_TRUE(fixture.runtime->NodeVisible("exportedProbe"));
    EXPECT_TRUE(fixture.runtime->NodeVisible("callbackProbe"));
    EXPECT_EQ(fixture.runtime->scriptErrorCount(), 0u);
}

TEST(SceneScriptMediaEventSmoke, DisabledMediaIntegrationSuppressesPlaybackEvents) {
    auto fixture = CreateRuntimeWithProbeNodes();
    ASSERT_NE(fixture.runtime, nullptr);

    fixture.runtime->RegisterSceneScript(
        R"JS(
function mediaPlaybackChanged(event) {
  scene.getObject('exportedProbe').visible = true;
}

scene.on('mediaPlaybackChanged', function(event) {
  scene.getObject('callbackProbe').visible = true;
});
)JS",
        "");
    ASSERT_EQ(fixture.runtime->sceneScriptCount(), 1u);

    fixture.runtime->SetMediaIntegrationEnabled(false);
    fixture.runtime->DispatchMediaEventJson(R"({"type":"mediaPlaybackChanged","state":0})");

    EXPECT_FALSE(fixture.runtime->NodeVisible("exportedProbe"));
    EXPECT_FALSE(fixture.runtime->NodeVisible("callbackProbe"));
    EXPECT_EQ(fixture.runtime->scriptErrorCount(), 0u);
}

} // namespace
} // namespace wallpaper
