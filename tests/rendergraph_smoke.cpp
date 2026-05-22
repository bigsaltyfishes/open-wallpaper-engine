#include "RenderGraph/RenderGraph.hpp"
#include "Scene/Scene.h"
#include "SpecTexs.hpp"
#include "VulkanRender/CopyPass.hpp"
#include "VulkanRender/CustomShaderPass.hpp"
#include "VulkanRender/SceneToRenderGraph.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace
{
using wallpaper::Scene;
using wallpaper::SceneCamera;
using wallpaper::SceneImageEffect;
using wallpaper::SceneImageEffectLayer;
using wallpaper::SceneImageEffectNode;
using wallpaper::SceneMaterial;
using wallpaper::SceneMesh;
using wallpaper::SceneNode;
using wallpaper::SceneRenderTarget;
using wallpaper::SpecTex_Default;
using wallpaper::vulkan::CopyPass;
using wallpaper::vulkan::CustomShaderPass;

struct GraphPassView {
    std::string             name;
    const CustomShaderPass* custom {};
    const CopyPass*         copy {};
};

std::shared_ptr<SceneMesh> makeMesh(std::string name, std::vector<std::string> textures = {}) {
    auto mesh = std::make_shared<SceneMesh>();
    SceneMaterial material {};
    material.name     = std::move(name);
    material.textures = std::move(textures);
    mesh->AddMaterial(std::move(material));
    return mesh;
}

std::shared_ptr<SceneNode> makeNode(
    std::string name,
    std::vector<std::string> textures = {}) {
    auto node = std::make_shared<SceneNode>();
    node->SetName(name);
    node->AddMesh(makeMesh(std::move(name), std::move(textures)));
    return node;
}

std::vector<GraphPassView> graphPasses(const wallpaper::rg::RenderGraph& graph) {
    std::vector<GraphPassView> passes;
    for (const auto id : graph.topologicalOrder()) {
        auto* pass_node = graph.getPassNode(id);
        assert(pass_node != nullptr);
        auto* pass = graph.getPass(id);
        assert(pass != nullptr);
        passes.push_back(GraphPassView {
            .name   = std::string(pass_node->name()),
            .custom = dynamic_cast<const CustomShaderPass*>(pass),
            .copy   = dynamic_cast<const CopyPass*>(pass),
        });
    }
    return passes;
}

const CustomShaderPass* findCustomPass(
    const std::vector<GraphPassView>& passes,
    const std::string& name) {
    const auto it = std::find_if(passes.begin(), passes.end(), [&name](const auto& pass) {
        return pass.custom != nullptr && pass.name == name;
    });
    assert(it != passes.end());
    return it->custom;
}

size_t findPassIndex(const std::vector<GraphPassView>& passes, const std::string& name) {
    const auto it = std::find_if(passes.begin(), passes.end(), [&name](const auto& pass) {
        return pass.name == name;
    });
    assert(it != passes.end());
    return static_cast<size_t>(std::distance(passes.begin(), it));
}

void installDefaultTargets(Scene& scene, bool default_reuse = true) {
    scene.activeCamera = nullptr;
    scene.renderTargets[std::string(SpecTex_Default)] = SceneRenderTarget {
        .width      = 1920,
        .height     = 1080,
        .allowReuse = default_reuse,
    };
    scene.cameras["effect"] = std::make_shared<SceneCamera>(1920, 1080, 0.01f, 100.0f);
}

void renderTargetAliasesResolveForOutputAndSampledSpecTextures() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_resolved"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };
    scene.renderTargetAliases["_alias_target"] = "_rt_resolved";

    auto sampled = makeNode("sampled_writer");
    scene.sceneGraph->AppendChild(sampled);

    auto sampler = makeNode("alias_sampler", { "_alias_target" });
    scene.sceneGraph->AppendChild(sampler);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        sampled.get(),
        64.0f,
        64.0f,
        "_alias_target",
        "_rt_pingpong_b"));
    sampled->SetCamera("alias_camera");
    scene.cameras["alias_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* output_pass = findCustomPass(passes, "sampled_writer");
    assert(output_pass->desc().output == "_rt_resolved");

    const auto* sampled_pass = findCustomPass(passes, "alias_sampler");
    assert(sampled_pass->desc().textures.size() == 1);
    assert(sampled_pass->desc().textures[0] == "_rt_resolved");
}

void runtimeTextTextureNamesStayImported() {
    Scene scene;
    installDefaultTargets(scene);
    scene.textures["runtime/text/Clock"].url = "runtime/text/Clock";

    auto text = makeNode("text", { "runtime/text/Clock" });
    scene.sceneGraph->AppendChild(text);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* text_pass = findCustomPass(passes, "text");
    assert(text_pass->desc().textures.size() == 1);
    assert(text_pass->desc().textures[0] == "runtime/text/Clock");

    const auto order = graph->topologicalOrder();
    const auto reads = graph->getLastReadTexs({ order });
    bool found_imported_text_texture = false;
    for (const auto& pass_reads : reads) {
        for (const auto* texture : pass_reads) {
            if (texture != nullptr && texture->key() == "runtime/text/Clock") {
                found_imported_text_texture =
                    texture->type() == wallpaper::rg::TexNode::TexType::Imported;
            }
        }
    }
    assert(found_imported_text_texture);
}

void submeshMaterialSlotsEmitDistinctCustomPasses() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_submesh_slots"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };

    auto node = std::make_shared<SceneNode>();
    node->SetName("multi_slot_mesh");

    auto mesh = std::make_shared<SceneMesh>();
    SceneMaterial body {};
    body.name = "body";
    SceneMaterial eyes {};
    eyes.name = "eyes";
    mesh->AddMaterial(std::move(body));
    mesh->AddMaterial(std::move(eyes));

    mesh->Submeshes().emplace_back();
    mesh->Submeshes().emplace_back();
    mesh->Submeshes()[0].material_slot = 0;
    mesh->Submeshes()[1].material_slot = 1;

    node->AddMesh(mesh);
    scene.sceneGraph->AppendChild(node);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        node.get(),
        64.0f,
        64.0f,
        "_rt_submesh_slots",
        "_rt_unused"));
    node->SetCamera("submesh_slots_camera");
    scene.cameras["submesh_slots_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* body_pass = findCustomPass(passes, "body");
    const auto* eyes_pass = findCustomPass(passes, "eyes");

    assert(body_pass->desc().output == "_rt_submesh_slots");
    assert(eyes_pass->desc().output == "_rt_submesh_slots");
    assert(body_pass->desc().submesh_index == 0);
    assert(body_pass->desc().material_slot == 0);
    assert(eyes_pass->desc().submesh_index == 1);
    assert(eyes_pass->desc().material_slot == 1);
    assert(body_pass->desc().clear_on_first_use);
    assert(!body_pass->desc().preserve_target_contents);
    assert(!eyes_pass->desc().clear_on_first_use);
    assert(eyes_pass->desc().preserve_target_contents);
}

void submeshMaterialRoutingDoesNotRequireSlotZero() {
    Scene scene;
    installDefaultTargets(scene);

    auto node = std::make_shared<SceneNode>();
    node->SetName("slot_one_only_mesh");

    auto mesh = std::make_shared<SceneMesh>();
    mesh->MaterialSlots().push_back(nullptr);
    SceneMaterial slot_one {};
    slot_one.name = "slot_one";
    mesh->AddMaterial(std::move(slot_one));

    mesh->Submeshes().emplace_back();
    mesh->Submeshes().emplace_back();
    mesh->Submeshes()[0].material_slot = 0;
    mesh->Submeshes()[1].material_slot = 1;

    node->AddMesh(mesh);
    scene.sceneGraph->AppendChild(node);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    assert(std::none_of(passes.begin(), passes.end(), [](const auto& pass) {
        return pass.custom != nullptr && pass.name.empty();
    }));

    const auto* slot_one_pass = findCustomPass(passes, "slot_one");
    assert(slot_one_pass->desc().output == SpecTex_Default);
    assert(slot_one_pass->desc().submesh_index == 1);
    assert(slot_one_pass->desc().material_slot == 1);
}

void skippedBasePassStillEmitsEffectPasses() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_fx_a"] = SceneRenderTarget { .width = 64, .height = 64 };
    scene.renderTargets["_rt_fx_b"] = SceneRenderTarget { .width = 64, .height = 64 };

    auto base = makeNode("skipped_base");
    base->SetSkipRenderPass(true);
    scene.sceneGraph->AppendChild(base);

    auto effect_node = makeNode("effect_after_skipped_base");
    auto effect      = std::make_shared<SceneImageEffect>();
    effect->nodes.push_back(SceneImageEffectNode {
        .output    = "_rt_fx_b",
        .sceneNode = effect_node,
    });

    auto layer = std::make_shared<SceneImageEffectLayer>(
        base.get(),
        64.0f,
        64.0f,
        "_rt_fx_a",
        "_rt_fx_b");
    layer->AddEffect(effect);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(layer);
    base->SetCamera("effect_camera");
    scene.cameras["effect_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    assert(std::none_of(passes.begin(), passes.end(), [](const auto& pass) {
        return pass.name == "skipped_base";
    }));
    assert(findCustomPass(passes, "effect_after_skipped_base") != nullptr);
}

void composeBaseRunsBeforeChildrenAndEffectsAfterChildren() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_compose_a"] = SceneRenderTarget { .width = 64, .height = 64 };
    scene.renderTargets["_rt_compose_b"] = SceneRenderTarget { .width = 64, .height = 64 };

    auto compose = makeNode("compose_base");
    scene.sceneGraph->AppendChild(compose);

    auto child = makeNode("compose_child");
    compose->AppendChild(child);

    auto effect_node = makeNode("compose_effect", { "_rt_compose_a" });
    auto effect      = std::make_shared<SceneImageEffect>();
    effect->nodes.push_back(SceneImageEffectNode {
        .output    = "_rt_compose_b",
        .sceneNode = effect_node,
    });

    auto layer = std::make_shared<SceneImageEffectLayer>(
        compose.get(),
        64.0f,
        64.0f,
        "_rt_compose_a",
        "_rt_compose_b");
    layer->AddEffect(effect);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchNode(compose);
    camera->AttatchImgEffect(layer);
    camera->SetComposeLayer(true);
    compose->SetCamera("compose_camera");
    scene.cameras["compose_camera"] = camera;
    scene.activeCamera              = camera.get();

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto base_index   = findPassIndex(passes, "compose_base");
    const auto child_index  = findPassIndex(passes, "compose_child");
    const auto effect_index = findPassIndex(passes, "compose_effect");

    assert(base_index < child_index);
    assert(child_index < effect_index);
}

void reusableNonDefaultTargetClearsOnlyOnFirstWriter() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_reusable"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };

    auto first = makeNode("first_reusable_writer");
    scene.sceneGraph->AppendChild(first);
    auto second = makeNode("second_reusable_writer");
    scene.sceneGraph->AppendChild(second);

    auto first_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    first_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        first.get(),
        64.0f,
        64.0f,
        "_rt_reusable",
        "_rt_unused_a"));
    first->SetCamera("first_camera");
    scene.cameras["first_camera"] = first_camera;

    auto second_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    second_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        second.get(),
        64.0f,
        64.0f,
        "_rt_reusable",
        "_rt_unused_b"));
    second->SetCamera("second_camera");
    scene.cameras["second_camera"] = second_camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* first_pass  = findCustomPass(passes, "first_reusable_writer");
    const auto* second_pass = findCustomPass(passes, "second_reusable_writer");

    assert(first_pass->desc().output == "_rt_reusable");
    assert(first_pass->desc().clear_on_first_use);
    assert(!first_pass->desc().preserve_target_contents);

    assert(second_pass->desc().output == "_rt_reusable");
    assert(!second_pass->desc().clear_on_first_use);
    assert(second_pass->desc().preserve_target_contents);
}

void forceClearReusableTargetClearsEveryWriter() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_force_clear"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
        .forceClear = true,
    };

    auto first = makeNode("first_force_clear_writer");
    scene.sceneGraph->AppendChild(first);
    auto second = makeNode("second_force_clear_writer");
    scene.sceneGraph->AppendChild(second);

    auto first_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    first_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        first.get(),
        64.0f,
        64.0f,
        "_rt_force_clear",
        "_rt_unused_a"));
    first->SetCamera("first_force_clear_camera");
    scene.cameras["first_force_clear_camera"] = first_camera;

    auto second_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    second_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        second.get(),
        64.0f,
        64.0f,
        "_rt_force_clear",
        "_rt_unused_b"));
    second->SetCamera("second_force_clear_camera");
    scene.cameras["second_force_clear_camera"] = second_camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* first_pass  = findCustomPass(passes, "first_force_clear_writer");
    const auto* second_pass = findCustomPass(passes, "second_force_clear_writer");

    assert(first_pass->desc().output == "_rt_force_clear");
    assert(first_pass->desc().clear_on_first_use);
    assert(!first_pass->desc().preserve_target_contents);

    assert(second_pass->desc().output == "_rt_force_clear");
    assert(second_pass->desc().clear_on_first_use);
    assert(!second_pass->desc().preserve_target_contents);
}

void forceClearDoesNotChangeDefaultOutputPreservation() {
    Scene scene;
    installDefaultTargets(scene);
    scene.clearEnabled = false;
    scene.renderTargets[std::string(SpecTex_Default)].forceClear = true;

    auto first = makeNode("first_default_force_clear_guard");
    scene.sceneGraph->AppendChild(first);
    auto second = makeNode("second_default_force_clear_guard");
    scene.sceneGraph->AppendChild(second);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* first_pass  = findCustomPass(passes, "first_default_force_clear_guard");
    const auto* second_pass = findCustomPass(passes, "second_default_force_clear_guard");

    assert(first_pass->desc().output == SpecTex_Default);
    assert(!first_pass->desc().clear_on_first_use);
    assert(!first_pass->desc().preserve_target_contents);

    assert(second_pass->desc().output == SpecTex_Default);
    assert(!second_pass->desc().clear_on_first_use);
    assert(second_pass->desc().preserve_target_contents);
}

void renderTargetSampleCountPropagatesToCustomPassDesc() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_msaa_scaffold"] = SceneRenderTarget {
        .width        = 64,
        .height       = 64,
        .allowReuse   = true,
        .sample_count = 4,
    };

    auto writer = makeNode("msaa_scaffold_writer");
    scene.sceneGraph->AppendChild(writer);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        writer.get(),
        64.0f,
        64.0f,
        "_rt_msaa_scaffold",
        "_rt_unused"));
    writer->SetCamera("msaa_scaffold_camera");
    scene.cameras["msaa_scaffold_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* pass = findCustomPass(passes, "msaa_scaffold_writer");
    assert(pass->desc().output == "_rt_msaa_scaffold");
    assert(pass->desc().sample_count == VK_SAMPLE_COUNT_4_BIT);
}

void copyWrittenMsaaTargetsPreserveWithSingleSamplePasses() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_msaa_after_copy"] = SceneRenderTarget {
        .width        = 64,
        .height       = 64,
        .allowReuse   = true,
        .sample_count = 4,
    };

    auto writer = makeNode("msaa_after_copy_writer");
    auto post   = std::make_shared<wallpaper::ScenePostProcess>();
    post->name  = "msaa_after_copy_post";
    post->steps.push_back(wallpaper::ScenePostProcessCopy {
        .src = std::string(SpecTex_Default),
        .dst = "_rt_msaa_after_copy",
    });
    post->steps.push_back(wallpaper::ScenePostProcessPass {
        .node   = writer,
        .output = "_rt_msaa_after_copy",
    });
    scene.post_processes.push_back(post);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* pass = findCustomPass(passes, "msaa_after_copy_writer");
    assert(pass->desc().output == "_rt_msaa_after_copy");
    assert(pass->desc().preserve_target_contents);
    assert(pass->desc().sample_count == VK_SAMPLE_COUNT_1_BIT);
}

void separateMsaaRenderTargetWritersPreserveWithMsaaSampleCount() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_msaa_multi_writer"] = SceneRenderTarget {
        .width        = 64,
        .height       = 64,
        .allowReuse   = true,
        .sample_count = 4,
    };

    auto first = makeNode("first_msaa_multi_writer");
    scene.sceneGraph->AppendChild(first);
    auto second = makeNode("second_msaa_multi_writer");
    scene.sceneGraph->AppendChild(second);

    auto first_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    first_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        first.get(),
        64.0f,
        64.0f,
        "_rt_msaa_multi_writer",
        "_rt_unused_a"));
    first->SetCamera("first_msaa_multi_writer_camera");
    scene.cameras["first_msaa_multi_writer_camera"] = first_camera;

    auto second_camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    second_camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        second.get(),
        64.0f,
        64.0f,
        "_rt_msaa_multi_writer",
        "_rt_unused_b"));
    second->SetCamera("second_msaa_multi_writer_camera");
    scene.cameras["second_msaa_multi_writer_camera"] = second_camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* first_pass  = findCustomPass(passes, "first_msaa_multi_writer");
    const auto* second_pass = findCustomPass(passes, "second_msaa_multi_writer");

    assert(first_pass->desc().output == "_rt_msaa_multi_writer");
    assert(first_pass->desc().clear_on_first_use);
    assert(!first_pass->desc().preserve_target_contents);
    assert(first_pass->desc().sample_count == VK_SAMPLE_COUNT_4_BIT);

    assert(second_pass->desc().output == "_rt_msaa_multi_writer");
    assert(!second_pass->desc().clear_on_first_use);
    assert(second_pass->desc().preserve_target_contents);
    assert(second_pass->desc().sample_count == VK_SAMPLE_COUNT_4_BIT);
}

void videoTextureSamplingIsPreservedInMsaaScenePass() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_msaa_video"] = SceneRenderTarget {
        .width        = 64,
        .height       = 64,
        .allowReuse   = true,
        .sample_count = 4,
    };
    scene.textures["video://clip"] = wallpaper::SceneTexture {
        .url     = "video://clip",
        .isVideo = true,
    };

    auto writer = makeNode("msaa_video_sampler", { "video://clip" });
    scene.sceneGraph->AppendChild(writer);

    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        writer.get(),
        64.0f,
        64.0f,
        "_rt_msaa_video",
        "_rt_unused"));
    writer->SetCamera("msaa_video_camera");
    scene.cameras["msaa_video_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* pass = findCustomPass(passes, "msaa_video_sampler");
    assert(pass->desc().output == "_rt_msaa_video");
    assert(pass->desc().textures.size() == 1);
    assert(pass->desc().textures[0] == "video://clip");
    assert(pass->desc().sample_count == VK_SAMPLE_COUNT_4_BIT);
}

void defaultOutputUsesSceneClearEnabledWhileNonDefaultWritesAlpha() {
    Scene scene;
    installDefaultTargets(scene);
    scene.clearEnabled = false;
    scene.renderTargets["_rt_alpha"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };

    auto default_node = makeNode("default_writer");
    scene.sceneGraph->AppendChild(default_node);

    auto offscreen_node = makeNode("alpha_writer");
    scene.sceneGraph->AppendChild(offscreen_node);
    auto camera = std::make_shared<SceneCamera>(64, 64, 0.01f, 100.0f);
    camera->AttatchImgEffect(std::make_shared<SceneImageEffectLayer>(
        offscreen_node.get(),
        64.0f,
        64.0f,
        "_rt_alpha",
        "_rt_unused"));
    offscreen_node->SetCamera("alpha_camera");
    scene.cameras["alpha_camera"] = camera;

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* default_pass = findCustomPass(passes, "default_writer");
    assert(default_pass->desc().output == SpecTex_Default);
    assert(!default_pass->desc().clear_on_first_use);
    assert(!default_pass->desc().write_alpha);

    const auto* offscreen_pass = findCustomPass(passes, "alpha_writer");
    assert(offscreen_pass->desc().output == "_rt_alpha");
    assert(offscreen_pass->desc().clear_on_first_use);
    assert(offscreen_pass->desc().write_alpha);

    Scene clear_scene;
    installDefaultTargets(clear_scene);
    clear_scene.clearEnabled = true;

    auto clear_node = makeNode("default_clear_writer");
    clear_scene.sceneGraph->AppendChild(clear_node);

    const auto clear_graph  = wallpaper::sceneToRenderGraph(clear_scene);
    const auto clear_passes = graphPasses(*clear_graph);

    const auto* clear_pass = findCustomPass(clear_passes, "default_clear_writer");
    assert(clear_pass->desc().output == SpecTex_Default);
    assert(clear_pass->desc().clear_on_first_use);
    assert(!clear_pass->desc().write_alpha);
}

void postProcessesAppendPassesAndCopiesAfterSceneGraph() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_post_pass"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };
    scene.renderTargets["_rt_post_copy"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };

    auto scene_node = makeNode("main_scene_writer");
    scene.sceneGraph->AppendChild(scene_node);

    auto post_node = makeNode("post_process_writer", { SpecTex_Default.data() });
    auto post      = std::make_shared<wallpaper::ScenePostProcess>();
    post->name     = "test_post_process";
    post->steps.push_back(wallpaper::ScenePostProcessPass {
        .node   = post_node,
        .output = "_rt_post_pass",
    });
    post->steps.push_back(wallpaper::ScenePostProcessCopy {
        .src = "_rt_post_pass",
        .dst = "_rt_post_copy",
    });
    scene.post_processes.push_back(post);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto scene_index = findPassIndex(passes, "main_scene_writer");
    const auto post_index  = findPassIndex(passes, "post_process_writer");
    const auto copy_it     = std::find_if(passes.begin(), passes.end(), [](const auto& pass) {
        return pass.copy != nullptr && pass.copy->desc().src == "_rt_post_pass"
            && pass.copy->desc().dst == "_rt_post_copy";
    });
    assert(copy_it != passes.end());
    const auto copy_index = static_cast<size_t>(std::distance(passes.begin(), copy_it));

    assert(scene_index < post_index);
    assert(post_index < copy_index);

    const auto* post_pass = findCustomPass(passes, "post_process_writer");
    assert(post_pass->desc().output == "_rt_post_pass");
    assert(copy_it->copy->desc().src == "_rt_post_pass");
    assert(copy_it->copy->desc().dst == "_rt_post_copy");
}

void nullPostProcessPassNodesAreSkipped() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_after_null_pass"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };
    scene.renderTargets["_rt_after_null_copy"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };

    auto post_node = makeNode("post_after_null_pass");
    auto post      = std::make_shared<wallpaper::ScenePostProcess>();
    post->name     = "null_pass_guard";
    post->steps.push_back(wallpaper::ScenePostProcessPass {
        .node   = nullptr,
        .output = "_rt_ignored_null_pass",
    });
    post->steps.push_back(wallpaper::ScenePostProcessPass {
        .node   = post_node,
        .output = "_rt_after_null_pass",
    });
    post->steps.push_back(wallpaper::ScenePostProcessCopy {
        .src = "_rt_after_null_pass",
        .dst = "_rt_after_null_copy",
    });
    scene.post_processes.push_back(post);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* post_pass = findCustomPass(passes, "post_after_null_pass");
    assert(post_pass->desc().output == "_rt_after_null_pass");
    assert(std::any_of(passes.begin(), passes.end(), [](const auto& pass) {
        return pass.copy != nullptr && pass.copy->desc().src == "_rt_after_null_pass"
            && pass.copy->desc().dst == "_rt_after_null_copy";
    }));
}

void postProcessCopyStepsResolveRenderTargetAliases() {
    Scene scene;
    installDefaultTargets(scene);
    scene.renderTargets["_rt_alias_resolved_src"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };
    scene.renderTargets["_rt_alias_resolved_dst"] = SceneRenderTarget {
        .width      = 64,
        .height     = 64,
        .allowReuse = true,
    };
    scene.renderTargetAliases["_alias_post_src"] = "_rt_alias_resolved_src";
    scene.renderTargetAliases["_alias_post_dst"] = "_rt_alias_resolved_dst";

    auto post_node = makeNode("alias_post_writer");
    auto post      = std::make_shared<wallpaper::ScenePostProcess>();
    post->name     = "alias_post_process";
    post->steps.push_back(wallpaper::ScenePostProcessPass {
        .node   = post_node,
        .output = "_alias_post_src",
    });
    post->steps.push_back(wallpaper::ScenePostProcessCopy {
        .src = "_alias_post_src",
        .dst = "_alias_post_dst",
    });
    scene.post_processes.push_back(post);

    const auto graph  = wallpaper::sceneToRenderGraph(scene);
    const auto passes = graphPasses(*graph);

    const auto* post_pass = findCustomPass(passes, "alias_post_writer");
    assert(post_pass->desc().output == "_rt_alias_resolved_src");

    const auto copy_it = std::find_if(passes.begin(), passes.end(), [](const auto& pass) {
        return pass.copy != nullptr && pass.copy->desc().src == "_rt_alias_resolved_src"
            && pass.copy->desc().dst == "_rt_alias_resolved_dst";
    });
    assert(copy_it != passes.end());

    const auto post_index = findPassIndex(passes, "alias_post_writer");
    const auto copy_index = static_cast<size_t>(std::distance(passes.begin(), copy_it));
    assert(post_index < copy_index);
}
} // namespace

int main() {
    renderTargetAliasesResolveForOutputAndSampledSpecTextures();
    runtimeTextTextureNamesStayImported();
    submeshMaterialSlotsEmitDistinctCustomPasses();
    submeshMaterialRoutingDoesNotRequireSlotZero();
    skippedBasePassStillEmitsEffectPasses();
    composeBaseRunsBeforeChildrenAndEffectsAfterChildren();
    reusableNonDefaultTargetClearsOnlyOnFirstWriter();
    forceClearReusableTargetClearsEveryWriter();
    forceClearDoesNotChangeDefaultOutputPreservation();
    renderTargetSampleCountPropagatesToCustomPassDesc();
    copyWrittenMsaaTargetsPreserveWithSingleSamplePasses();
    separateMsaaRenderTargetWritersPreserveWithMsaaSampleCount();
    videoTextureSamplingIsPreservedInMsaaScenePass();
    defaultOutputUsesSceneClearEnabledWhileNonDefaultWritesAlpha();
    postProcessesAppendPassesAndCopiesAfterSceneGraph();
    nullPostProcessPassNodesAreSkipped();
    postProcessCopyStepsResolveRenderTargetAliases();
    return 0;
}
