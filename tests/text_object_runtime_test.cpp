#include "Audio/SoundManager.h"
#include "Fs/Fs.h"
#include "Fs/MemBinaryStream.h"
#include "Fs/VFS.h"
#include "Interface/IImageParser.h"
#include "Runtime/DynamicValue.hpp"
#include "Runtime/SceneRuntimeContext.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scripting/ScriptEngine.hpp"
#include "Project/ProjectProperties.hpp"
#include "SpecTexs.hpp"
#include "Text/SystemFontResolver.hpp"
#include "Text/TextLayer.hpp"
#include "Vulkan/Shader.hpp"
#include "WPSceneParser.hpp"

#include <gtest/gtest.h>
#include <spirv_reflect.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wallpaper
{
namespace
{

class MemoryFs final : public fs::Fs {
public:
    explicit MemoryFs(std::map<std::string, std::string> files): m_files(std::move(files)) {}

    bool Contains(std::string_view path) const override {
        return m_files.contains(std::string(path));
    }

    std::shared_ptr<fs::IBinaryStream> Open(std::string_view path) override {
        const auto it = m_files.find(std::string(path));
        if (it == m_files.end()) return nullptr;
        const auto& s = it->second;
        return std::make_shared<fs::MemBinaryStream>(std::vector<uint8_t>(s.begin(), s.end()));
    }

    std::shared_ptr<fs::IBinaryStreamW> OpenW(std::string_view) override { return nullptr; }

private:
    std::map<std::string, std::string> m_files;
};

void MountAssets(fs::VFS& vfs, std::map<std::string, std::string> files = {}) {
    ASSERT_TRUE(vfs.Mount("/assets", std::make_unique<MemoryFs>(std::move(files))));
}

std::string MinimalSceneObjects(std::string objects_json) {
    return R"({
      "camera": {"center":[0,0,0], "eye":[0,0,1], "up":[0,1,0]},
      "general": {
        "ambientcolor":[0,0,0], "skylightcolor":[0,0,0],
        "clearcolor":[0,0,0], "cameraparallax":false,
        "cameraparallaxamount":0, "cameraparallaxdelay":0,
        "cameraparallaxmouseinfluence":0,
        "orthogonalprojection":{"width":640,"height":360}
      },
      "objects": )" +
           objects_json + "\n}";
}

SceneNode* FindRootChild(Scene& scene, std::string_view name) {
    if (scene.sceneGraph == nullptr) return nullptr;
    const auto& children = scene.sceneGraph->GetChildren();
    const auto  it       = std::find_if(children.begin(), children.end(), [name](const auto& node) {
        return node != nullptr && node->Name() == name;
    });
    return it == children.end() ? nullptr : it->get();
}

bool ShaderUsesUniformMember(const ShaderCode& spirv, std::string_view member_name) {
    spv_reflect::ShaderModule module(spirv, SPV_REFLECT_MODULE_FLAG_NO_COPY);

    uint32_t binding_count = 0;
    if (module.EnumerateDescriptorBindings(&binding_count, nullptr) != SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }

    std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
    if (module.EnumerateDescriptorBindings(&binding_count, bindings.data()) !=
        SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }

    for (const auto* binding : bindings) {
        if (binding == nullptr || ! binding->accessed ||
            binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            continue;
        }
        const auto& block = binding->block;
        for (uint32_t index = 0; index < block.member_count; ++index) {
            const auto* name = block.members[index].name;
            if (name != nullptr && member_name == name) return true;
        }
    }
    return false;
}

std::optional<VkDescriptorSetLayoutBinding>
ShaderDescriptorBinding(const std::vector<ShaderCode>& spirv, std::string_view name) {
    std::vector<vulkan::Uni_ShaderSpv> spvs;
    vulkan::ShaderReflected            reflected;
    if (! vulkan::GenReflect(spirv, spvs, reflected)) return std::nullopt;

    const auto iterator = reflected.binding_map.find(std::string(name));
    if (iterator == reflected.binding_map.end()) return std::nullopt;

    return iterator->second;
}

std::vector<VkDescriptorSetLayoutBinding>
ShaderDescriptorBindings(const std::vector<ShaderCode>& spirv) {
    std::vector<vulkan::Uni_ShaderSpv> spvs;
    vulkan::ShaderReflected            reflected;
    if (! vulkan::GenReflect(spirv, spvs, reflected)) return {};

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(reflected.binding_map.size());
    for (const auto& [name, binding] : reflected.binding_map) {
        (void)name;
        bindings.push_back(binding);
    }
    return bindings;
}

} // namespace

TEST(TextObjectRuntime, ParserCreatesRuntimeTextNodeAndState) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "text-object",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": {"value": "hello"},
            "font": {"value": "Arial"},
            "pointsize": 20,
            "padding": 4,
            "origin": [100, 50, 0],
            "horizontalalign": "right",
            "verticalalign": "bottom",
            "alignment": "left",
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_TRUE(scene->runtime->HasNodeNamed("caption"));
    EXPECT_EQ(scene->runtime->NodeText("caption"), "hello");

    const auto size = scene->runtime->NodeSize("caption");
    EXPECT_GT(size.x(), 8.0f);
    EXPECT_GT(size.y(), 8.0f);

    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    EXPECT_NE(node->Mesh(), nullptr);
    EXPECT_LT(node->Translate().x(), 100.0f);
    EXPECT_GT(node->Translate().y(), 50.0f);
}

TEST(TextObjectRuntime, ParserCreatesRenderableTextMaterialAndTexture) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "text-renderable-material",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "I I",
            "font": "Arial",
            "pointsize": 20,
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->Mesh(), nullptr);

    auto* material = node->Mesh()->MaterialForSlot(0);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->name, "text");
    ASSERT_EQ(material->textures.size(), 1u);
    EXPECT_FALSE(material->textures.front().empty());
    EXPECT_FALSE(IsSpecTex(material->textures.front()));
    EXPECT_TRUE(scene->textures.contains(material->textures.front()));
    auto image = scene->imageParser->Parse(material->textures.front());
    ASSERT_NE(image, nullptr);
    ASSERT_EQ(image->slots.size(), 1u);
    ASSERT_EQ(image->slots[0].mipmaps.size(), 1u);
    const auto& mip = image->slots[0].mipmaps[0];
    ASSERT_NE(mip.data, nullptr);
    ASSERT_GT(mip.width, 1);
    ASSERT_GT(mip.height, 1);
    bool has_visible_alpha            = false;
    bool has_transparent_space_column = false;
    for (int y = 1; y < mip.height - 1; ++y) {
        for (int x = 1; x < mip.width - 1; ++x) {
            const auto alpha =
                mip.data.get()[(static_cast<std::size_t>(y) * mip.width + x) * 4u + 3u];
            has_visible_alpha = has_visible_alpha || alpha > 0u;
        }
    }
    const int min_space_x = mip.width / 4;
    const int max_space_x = (mip.width * 3) / 4;
    for (int x = min_space_x; x < max_space_x && ! has_transparent_space_column; ++x) {
        bool column_clear = true;
        for (int y = 1; y < mip.height - 1; ++y) {
            const auto alpha =
                mip.data.get()[(static_cast<std::size_t>(y) * mip.width + x) * 4u + 3u];
            if (alpha != 0u) {
                column_clear = false;
                break;
            }
        }
        has_transparent_space_column = column_clear;
    }
    EXPECT_TRUE(has_visible_alpha);
    EXPECT_TRUE(has_transparent_space_column);
    ASSERT_NE(material->customShader.shader, nullptr);
    ASSERT_FALSE(material->customShader.shader->codes.empty());
    EXPECT_TRUE(ShaderUsesUniformMember(material->customShader.shader->codes.front(),
                                        "g_ModelViewProjectionMatrix"));
    const auto texture_binding =
        ShaderDescriptorBinding(material->customShader.shader->codes, "g_Texture0");
    ASSERT_TRUE(texture_binding.has_value());
    EXPECT_EQ(texture_binding->descriptorType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_NE(texture_binding->binding, 0u);
    const auto descriptor_bindings = ShaderDescriptorBindings(material->customShader.shader->codes);
    ASSERT_FALSE(descriptor_bindings.empty());
    for (const auto& binding : descriptor_bindings) {
        if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            EXPECT_NE(binding.binding, texture_binding->binding);
        }
    }
    EXPECT_EQ(material->blenmode, BlendMode::Translucent);
}

TEST(TextObjectRuntime, ExplicitTextObjectSizeControlsRuntimeTextureDimensions) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "text-explicit-size-texture",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "clock",
            "text": "12:34",
            "font": "Arial",
            "pointsize": 20,
            "size": [320, 90],
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "clock");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->Mesh(), nullptr);
    auto* material = node->Mesh()->MaterialForSlot(0);
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->textures.size(), 1u);

    auto image = scene->imageParser->Parse(material->textures.front());
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->header.width, 320);
    EXPECT_EQ(image->header.height, 90);

    ASSERT_TRUE(scene->runtime->SetNodeText("clock", "12:34:56 PM"));
    scene->runtime->PumpTextLayerCache();

    auto updated = scene->imageParser->Parse(material->textures.front());
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->header.width, 320);
    EXPECT_EQ(updated->header.height, 90);
    const auto runtime_size = scene->runtime->NodeSize("clock");
    EXPECT_FLOAT_EQ(runtime_size.x(), 320.0f);
    EXPECT_FLOAT_EQ(runtime_size.y(), 90.0f);
}

TEST(TextObjectRuntime, TextStyleColorAlphaAndBrightnessTintRuntimeTexture) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "text-style-tint",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "IIII",
            "font": "Arial",
            "pointsize": 20,
            "color": [0.25, 0.5, 0.75],
            "brightness": 0.5,
            "alpha": 0.25,
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->Mesh(), nullptr);
    auto* material = node->Mesh()->MaterialForSlot(0);
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->textures.size(), 1u);

    auto image = scene->imageParser->Parse(material->textures.front());
    ASSERT_NE(image, nullptr);
    ASSERT_EQ(image->slots.size(), 1u);
    ASSERT_EQ(image->slots[0].mipmaps.size(), 1u);
    const auto& mip = image->slots[0].mipmaps[0];
    ASSERT_NE(mip.data, nullptr);

    bool saw_tinted_pixel         = false;
    bool saw_straight_alpha_pixel = false;
    int  max_alpha                = 0;
    for (int y = 0; y < mip.height; ++y) {
        for (int x = 0; x < mip.width; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * mip.width + static_cast<std::size_t>(x)) * 4u;
            const auto alpha = mip.data.get()[offset + 3u];
            max_alpha        = std::max(max_alpha, static_cast<int>(alpha));
            if (alpha == 0u || saw_tinted_pixel) continue;

            EXPECT_LT(mip.data.get()[offset + 0u], mip.data.get()[offset + 1u]);
            EXPECT_LT(mip.data.get()[offset + 1u], mip.data.get()[offset + 2u]);
            saw_tinted_pixel = true;
        }
    }
    for (int y = 0; y < mip.height && ! saw_straight_alpha_pixel; ++y) {
        for (int x = 0; x < mip.width; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * mip.width + static_cast<std::size_t>(x)) * 4u;
            const auto alpha = mip.data.get()[offset + 3u];
            if (alpha < 48u) continue;

            EXPECT_GE(mip.data.get()[offset + 0u], 24u);
            EXPECT_GE(mip.data.get()[offset + 1u], 48u);
            EXPECT_GE(mip.data.get()[offset + 2u], 72u);
            saw_straight_alpha_pixel = true;
            break;
        }
    }
    EXPECT_TRUE(saw_tinted_pixel);
    EXPECT_TRUE(saw_straight_alpha_pixel);
    EXPECT_LE(max_alpha, 64);
}

TEST(TextObjectRuntime, RasterizedTextUsesFontPixelSizeAndAntialiasesEdges) {
    TextLayerState state {
        .text                   = "Hg",
        .font_key               = "systemfont_Helvetica",
        .resolved_font_kind     = "system",
        .resolved_font_identity = "Helvetica",
        .resolved_font_path     = ResolveSystemFontPath("systemfont_Helvetica"),
        .point_size             = 40.0f,
        .padding                = 4.0f,
    };
#ifdef __APPLE__
    ASSERT_FALSE(state.resolved_font_path.empty());
#endif

    constexpr uint32_t   width  = 160;
    constexpr uint32_t   height = 80;
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u, 0u);
    RasterizeTextLayer(state, width, height, rgba);

    int  min_y                  = static_cast<int>(height);
    int  max_y                  = -1;
    bool has_partial_alpha_edge = false;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto alpha = rgba[(static_cast<std::size_t>(y) * width + x) * 4u + 3u];
            if (alpha == 0u) continue;
            min_y                  = std::min(min_y, static_cast<int>(y));
            max_y                  = std::max(max_y, static_cast<int>(y));
            has_partial_alpha_edge = has_partial_alpha_edge || (alpha > 0u && alpha < 255u);
        }
    }

    ASSERT_GE(max_y, min_y);
    EXPECT_GT(max_y - min_y + 1, 28);
    EXPECT_TRUE(has_partial_alpha_edge);
}

TEST(TextObjectRuntime, ParserResolvesFamilyFontsForFreeTypeRasterization) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "text-family-freetype",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "Hg",
            "font": "Helvetica",
            "pointsize": 40,
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    const auto state = scene->runtime->NodeTextState("caption");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->resolved_font_kind, "family");
#ifdef __APPLE__
    EXPECT_FALSE(state->resolved_font_path.empty());
#endif
    const auto runtime_size = scene->runtime->NodeSize("caption");
    EXPECT_GT(runtime_size.x(), 120.0f);
    EXPECT_GT(runtime_size.y(), 120.0f);

    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->Mesh(), nullptr);
    const auto& vertex_array = node->Mesh()->GetVertexArray(0);
    ASSERT_EQ(vertex_array.VertexCount(), 4u);
    const float* vertices = vertex_array.Data();
    ASSERT_NE(vertices, nullptr);
    const auto stride = vertex_array.OneSize();
    float      min_x  = vertices[0];
    float      max_x  = vertices[0];
    float      min_y  = vertices[1];
    float      max_y  = vertices[1];
    for (std::size_t index = 1; index < vertex_array.VertexCount(); ++index) {
        const float x = vertices[index * stride + 0u];
        const float y = vertices[index * stride + 1u];
        min_x         = std::min(min_x, x);
        max_x         = std::max(max_x, x);
        min_y         = std::min(min_y, y);
        max_y         = std::max(max_y, y);
    }
    EXPECT_NEAR(max_x - min_x, runtime_size.x(), 1.0f);
    EXPECT_NEAR(max_y - min_y, runtime_size.y(), 1.0f);

    auto* material = node->Mesh()->MaterialForSlot(0);
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->textures.size(), 1u);
    auto image = scene->imageParser->Parse(material->textures.front());
    ASSERT_NE(image, nullptr);
    ASSERT_EQ(image->slots.size(), 1u);
    ASSERT_EQ(image->slots[0].mipmaps.size(), 1u);
    const auto& mip = image->slots[0].mipmaps[0];
    ASSERT_NE(mip.data, nullptr);

    bool has_partial_alpha_edge = false;
    for (int y = 0; y < mip.height && ! has_partial_alpha_edge; ++y) {
        for (int x = 0; x < mip.width; ++x) {
            const auto alpha =
                mip.data.get()[(static_cast<std::size_t>(y) * mip.width + x) * 4u + 3u];
            if (alpha > 0u && alpha < 255u) {
                has_partial_alpha_edge = true;
                break;
            }
        }
    }
    EXPECT_TRUE(has_partial_alpha_edge);
}

TEST(TextObjectRuntime, FreeTypeEstimatedSizeContainsTallGlyphs) {
    TextLayerState state {
        .text                   = "Hg",
        .font_key               = "systemfont_Helvetica",
        .resolved_font_kind     = "system",
        .resolved_font_identity = "Helvetica",
        .resolved_font_path     = ResolveSystemFontPath("systemfont_Helvetica"),
        .point_size             = 40.0f,
        .padding                = 4.0f,
    };
#ifdef __APPLE__
    ASSERT_FALSE(state.resolved_font_path.empty());
#endif

    const auto size = TextLayerRasterSize(state);
    EXPECT_GT(size.x(), 120.0f);
    EXPECT_GT(size.y(), 120.0f);

    std::vector<uint8_t> rgba(static_cast<std::size_t>(std::ceil(size.x())) *
                                  static_cast<std::size_t>(std::ceil(size.y())) * 4u,
                              0u);
    RasterizeTextLayer(state,
                       static_cast<uint32_t>(std::ceil(size.x())),
                       static_cast<uint32_t>(std::ceil(size.y())),
                       rgba);

    bool has_visible_alpha = false;
    for (std::size_t offset = 3; offset < rgba.size(); offset += 4u) {
        if (rgba[offset] > 0u) {
            has_visible_alpha = true;
            break;
        }
    }
    EXPECT_TRUE(has_visible_alpha);
}

TEST(TextObjectRuntime, DynamicTextTransformSettingsUpdateRuntimeNode) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties {
          { "text_origin", RuntimeScalarValue::String("30 40 0") },
          { "text_scale", RuntimeScalarValue::Float(2.5f) },
    };
    SceneParseRequest request {
        .scene_id           = "text-dynamic-transform",
        .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "clock",
            "font": "Arial",
            "pointsize": 20,
            "origin": {"value": "10 20 0", "user": "text_origin"},
            "scale": {"value": "1 1 1", "user": "text_scale"},
            "horizontalalign": "center",
            "verticalalign": "center",
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);

    EXPECT_FLOAT_EQ(node->Translate().x(), 30.0f);
    EXPECT_FLOAT_EQ(node->Translate().y(), 40.0f);
    EXPECT_FLOAT_EQ(node->Scale().x(), 2.5f);
    EXPECT_FLOAT_EQ(node->Scale().y(), 2.5f);

    scene->runtime->ApplyProjectPropertyOverride({
        { "text_origin", RuntimeScalarValue::String("50 60 0") },
        { "text_scale", RuntimeScalarValue::Float(3.0f) },
    });
    scene->runtime->Tick(1.0 / 60.0);

    EXPECT_FLOAT_EQ(node->Translate().x(), 50.0f);
    EXPECT_FLOAT_EQ(node->Translate().y(), 60.0f);
    EXPECT_FLOAT_EQ(node->Scale().x(), 3.0f);
    EXPECT_FLOAT_EQ(node->Scale().y(), 3.0f);
}

TEST(TextObjectRuntime, DynamicTextTransformScriptsUseCanvasSize) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties {
          { "x", RuntimeScalarValue::Float(0.8f) },
          { "y", RuntimeScalarValue::Float(0.25f) },
    };
    SceneParseRequest request {
        .scene_id           = "text-dynamic-transform-script",
        .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              R"JSON({
          "camera": {"center":[0,0,0], "eye":[0,0,1], "up":[0,1,0]},
          "general": {
            "ambientcolor":[0,0,0], "skylightcolor":[0,0,0],
            "clearcolor":[0,0,0], "cameraparallax":false,
            "cameraparallaxamount":0, "cameraparallaxdelay":0,
            "cameraparallaxmouseinfluence":0,
            "orthogonalprojection":{"width":1000,"height":500}
          },
          "objects": [
            {
              "id": 1,
              "name": "caption",
              "text": "clock",
              "font": "Arial",
              "pointsize": 20,
              "origin": {
                "value": "0 0 0",
                "scriptproperties": {
                  "x": {"user": "x", "value": 0.5},
                  "y": {"user": "y", "value": 0.5}
                },
                "script": "export var scriptProperties = createScriptProperties().addSlider({name:'x',value:0.5}).addSlider({name:'y',value:0.5}).finish(); export function update(value) { value.x = scriptProperties.x * engine.canvasSize.x; value.y = scriptProperties.y * engine.canvasSize.y; return value; }"
              },
              "horizontalalign": "center",
              "verticalalign": "center",
              "visible": true
            }
          ]
        })JSON",
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);

    scene->runtime->Tick(1.0 / 60.0);

    EXPECT_FLOAT_EQ(node->Translate().x(), 800.0f);
    EXPECT_FLOAT_EQ(node->Translate().y(), 125.0f);
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, ParserUsesUniqueTextureKeysForDuplicateTextLayerNames) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;

    ProjectProperties properties;
    SceneParseRequest request {
        .scene_id           = "duplicate-text-names",
        .project_properties = &properties,
    };
    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "duplicate",
            "text": "first",
            "font": "Arial",
            "pointsize": 20,
            "visible": true
          },
          {
            "id": 2,
            "name": "duplicate",
            "text": "second much wider",
            "font": "Arial",
            "pointsize": 20,
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->sceneGraph, nullptr);
    auto* first  = FindRootChild(*scene, "__we_text_1");
    auto* second = FindRootChild(*scene, "__we_text_2");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first->Mesh(), nullptr);
    ASSERT_NE(second->Mesh(), nullptr);
    auto* first_material  = first->Mesh()->MaterialForSlot(0);
    auto* second_material = second->Mesh()->MaterialForSlot(0);
    ASSERT_NE(first_material, nullptr);
    ASSERT_NE(second_material, nullptr);
    ASSERT_EQ(first_material->textures.size(), 1u);
    ASSERT_EQ(second_material->textures.size(), 1u);

    EXPECT_NE(first_material->textures.front(), second_material->textures.front());
    EXPECT_TRUE(scene->runtime->HasNodeNamed("__we_text_1"));
    EXPECT_TRUE(scene->runtime->HasNodeNamed("__we_text_2"));
    EXPECT_EQ(scene->runtime->NodeText("__we_text_1"), "first");
    EXPECT_EQ(scene->runtime->NodeText("__we_text_2"), "second much wider");
}

TEST(TextObjectRuntime, ParserReadsSupportedTextFormsIntoRuntimeState) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-forms",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "plain", "text": "plain text", "font": "Arial", "visible": true},
          {"id": 2, "name": "nested", "text": {"text": "nested text"}, "font": {"value": "Arial"}, "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_EQ(scene->runtime->NodeText("plain"), "plain text");
    EXPECT_EQ(scene->runtime->NodeText("nested"), "nested text");
}

TEST(TextObjectRuntime, ParserResolvesFontFamiliesAssetsAndSystemFontAliases) {
    fs::VFS vfs;
    MountAssets(vfs,
                {
                    { "/fonts/asset.ttf", "fake-font-bytes" },
                    { "/fonts/prefixed.ttf", "fake-font-bytes" },
                });
    ASSERT_TRUE(vfs.Mount("/provided",
                          std::make_unique<MemoryFs>(std::map<std::string, std::string> {
                              { "/absolute.otf", "fake-font-bytes" },
                          })));
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-fonts",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "family", "text": "a", "font": "Arial", "visible": true},
          {"id": 2, "name": "asset", "text": "a", "font": "fonts/asset.ttf", "visible": true},
          {"id": 3, "name": "system", "text": "a", "font": "systemfont_Helvetica", "visible": true},
          {"id": 4, "name": "provided", "text": "a", "font": "/provided/absolute.otf", "visible": true},
          {"id": 5, "name": "prefixed", "text": "a", "font": "assets/fonts/prefixed.ttf", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);

    auto family = scene->runtime->NodeTextState("family");
    ASSERT_TRUE(family.has_value());
    EXPECT_EQ(family->resolved_font_kind, "family");
    EXPECT_EQ(family->resolved_font_identity, "Arial");

    auto asset = scene->runtime->NodeTextState("asset");
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->resolved_font_kind, "asset");
    EXPECT_EQ(asset->resolved_font_path, "/assets/fonts/asset.ttf");
    EXPECT_EQ(std::string(asset->resolved_font_data.begin(), asset->resolved_font_data.end()),
              "fake-font-bytes");

    auto system = scene->runtime->NodeTextState("system");
    ASSERT_TRUE(system.has_value());
    EXPECT_EQ(system->resolved_font_kind, "system");
    EXPECT_EQ(system->resolved_font_identity, "Helvetica");

    auto provided = scene->runtime->NodeTextState("provided");
    ASSERT_TRUE(provided.has_value());
    EXPECT_EQ(provided->resolved_font_kind, "asset");
    EXPECT_EQ(provided->resolved_font_path, "/provided/absolute.otf");
    EXPECT_EQ(std::string(provided->resolved_font_data.begin(), provided->resolved_font_data.end()),
              "fake-font-bytes");

    auto prefixed = scene->runtime->NodeTextState("prefixed");
    ASSERT_TRUE(prefixed.has_value());
    EXPECT_EQ(prefixed->resolved_font_kind, "asset");
    EXPECT_EQ(prefixed->resolved_font_path, "/assets/fonts/prefixed.ttf");
}

TEST(TextObjectRuntime, ParserMapsSystemFontAliasesToPlatformFontPath) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-system-font-path",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "system", "text": "a", "font": "systemfont_Helvetica", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);

    auto system = scene->runtime->NodeTextState("system");
    ASSERT_TRUE(system.has_value());
    EXPECT_EQ(system->resolved_font_kind, "system");
    EXPECT_EQ(system->resolved_font_identity, "Helvetica");
#ifdef __APPLE__
    EXPECT_FALSE(system->resolved_font_path.empty());
#else
    EXPECT_TRUE(system->resolved_font_path.empty());
#endif
}

TEST(TextObjectRuntime, HiddenTextStillCreatesNodeAndRuntimeState) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "hidden-text",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "hiddenCaption", "text": "secret", "font": "Arial", "visible": false}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_TRUE(scene->runtime->HasNodeNamed("hiddenCaption"));
    EXPECT_EQ(scene->runtime->NodeText("hiddenCaption"), "secret");
    auto* node = FindRootChild(*scene, "hiddenCaption");
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(node->Visible());
}

TEST(TextObjectRuntime, TextVisibleScriptUpdatesVisibilityOnTick) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-visible-script",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "scripted",
            "font": "Arial",
            "visible": {
              "value": true,
              "script": "export function update(value) { return false; }"
            }
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);

    scene->runtime->Tick(1.0 / 60.0);
    EXPECT_FALSE(node->Visible());
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, TextVisibleUserBindingFollowsProjectOverride) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties {
          { "show_text", RuntimeScalarValue::Bool(true) },
    };
    SceneParseRequest request {
        .scene_id           = "text-visible-user",
        .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": "user",
            "font": "Arial",
            "visible": {"value": true, "user": "show_text"}
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->Visible());

    scene->runtime->ApplyProjectPropertyOverride({
        { "show_text", RuntimeScalarValue::Bool(false) },
    });
    scene->runtime->Tick(1.0 / 60.0);
    EXPECT_FALSE(node->Visible());
}

TEST(TextObjectRuntime, TextFieldScriptUpdatesRuntimeTextOnTick) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-field-script",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": {
              "value": "before",
              "script": "export function update(value) { return value + ' after'; }"
            },
            "font": "Arial",
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_EQ(scene->runtime->NodeText("caption"), "before");

    scene->runtime->Tick(1.0 / 60.0);

    EXPECT_EQ(scene->runtime->NodeText("caption"), "before after");
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, StaticObjectTextDoesNotOverwriteRuntimeMutationOnTick) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-static-object-field",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {
            "id": 1,
            "name": "caption",
            "text": {"text": "before"},
            "font": "Arial",
            "visible": true
          }
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_EQ(scene->runtime->NodeText("caption"), "before");

    scene->runtime->SetNodeText("caption", "after");
    scene->runtime->Tick(1.0 / 60.0);

    EXPECT_EQ(scene->runtime->NodeText("caption"), "after");
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, EventOnlyTextScriptDoesNotOverwriteRuntimeMutationOnTick) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-event-script-object-field",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"JSON([
          {
            "id": 1,
            "name": "caption",
            "text": {
              "value": "before",
              "script": "engine.on('custom', function() {})"
            },
            "font": "Arial",
            "visible": true
          }
        ])JSON"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_EQ(scene->runtime->NodeText("caption"), "before");

    scene->runtime->SetNodeText("caption", "after");
    scene->runtime->Tick(1.0 / 60.0);

    EXPECT_EQ(scene->runtime->NodeText("caption"), "after");
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, EventOnlyTextScriptRegistersAsSceneScript) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-event-script-scene-script",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"JSON([
          {
            "id": 1,
            "name": "caption",
            "text": {
              "value": "before",
              "script": "engine.on('cursorDown', function() { thisLayer.text = 'clicked'; })"
            },
            "font": "Arial",
            "visible": true
          }
        ])JSON"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_EQ(scene->runtime->NodeText("caption"), "before");
    EXPECT_EQ(scene->runtime->sceneScriptCount(), 1u);

    scene->runtime->DispatchCursorDown(0);

    EXPECT_EQ(scene->runtime->NodeText("caption"), "clicked");
    EXPECT_EQ(scene->runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, TextParentReusesPlaceholderWhenChildAppearsFirst) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-placeholder",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 2, "name": "childText", "parent": 1, "text": "child", "font": "Arial", "visible": true},
          {"id": 1, "name": "parentText", "text": "parent", "font": "Arial", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    auto* parent = FindRootChild(*scene, "parentText");
    ASSERT_NE(parent, nullptr);
    EXPECT_NE(parent->Mesh(), nullptr);
    ASSERT_EQ(parent->GetChildren().size(), 1u);
    EXPECT_EQ(parent->GetChildren().front()->Name(), "childText");
    EXPECT_EQ(parent->GetChildren().front()->Parent(), parent);
}

TEST(TextObjectRuntime, UnnamedTextPlaceholderDoesNotLeaveStaleRuntimeLayerKey) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "unnamed-text-placeholder",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 2, "name": "childText", "parent": 1, "text": "child", "font": "Arial", "visible": true},
          {"id": 1, "text": "parent", "font": "Arial", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    EXPECT_FALSE(scene->runtime->HasNodeNamed("__we_layer_1"));
    EXPECT_TRUE(scene->runtime->HasNodeNamed("__we_text_1"));
    EXPECT_EQ(scene->runtime->NodeText("__we_text_1"), "parent");
}

TEST(TextObjectRuntime, TextStateUsesRuntimeRgbaTextureBackend) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(runtime, nullptr);

    auto node = std::make_shared<SceneNode>();
    runtime->RegisterNode("caption", node.get());
    runtime->RegisterTextLayer("caption",
                               TextLayerState {
                                   .text       = "layout only",
                                   .font_key   = "Arial",
                                   .point_size = 12.0f,
                               });

    const auto state = runtime->NodeTextState("caption");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->render_backend, "runtime-rgba-texture");
}

TEST(TextObjectRuntime, RuntimeTextMutationUpdatesPersistentTextAndSize) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(runtime, nullptr);

    auto node = std::make_shared<SceneNode>();
    runtime->RegisterNode("caption", node.get());
    runtime->RegisterTextLayer("caption",
                               TextLayerState {
                                   .text             = "a",
                                   .font_key         = "Arial",
                                   .point_size       = 10.0f,
                                   .padding          = 2.0f,
                                   .horizontal_align = "left",
                                   .vertical_align   = "top",
                                   .anchor           = "left top",
                               });

    const auto before       = runtime->NodeSize("caption");
    const auto before_state = runtime->NodeTextState("caption");
    ASSERT_TRUE(before_state.has_value());
    EXPECT_FALSE(before_state->texture_cache_key.empty());
    EXPECT_EQ(before_state->cache_revision, 1u);
    ASSERT_TRUE(runtime->SetNodeText("caption", "longer text"));

    EXPECT_EQ(runtime->NodeText("caption"), "longer text");
    const auto after = runtime->NodeSize("caption");
    EXPECT_GT(after.x(), before.x());
    EXPECT_FLOAT_EQ(after.y(), before.y());
    EXPECT_TRUE(runtime->NodeTextDirty("caption"));
    const auto dirty_state = runtime->NodeTextState("caption");
    ASSERT_TRUE(dirty_state.has_value());
    EXPECT_EQ(dirty_state->cache_revision, before_state->cache_revision + 1u);
    EXPECT_TRUE(dirty_state->cache_dirty);
    EXPECT_TRUE(dirty_state->full_dirty);

    runtime->ClearNodeTextDirty("caption");
    const auto clean_state = runtime->NodeTextState("caption");
    ASSERT_TRUE(clean_state.has_value());
    EXPECT_EQ(clean_state->text, "longer text");
    EXPECT_EQ(clean_state->cache_revision, dirty_state->cache_revision);
    EXPECT_FALSE(clean_state->cache_dirty);
    EXPECT_FALSE(clean_state->full_dirty);
    EXPECT_FALSE(runtime->NodeTextDirty("caption"));
}

TEST(TextObjectRuntime, AlignedTextReanchorsWhenTextSizeChanges) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-reanchor",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "caption", "text": "a", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "horizontalalign": "right", "verticalalign": "bottom", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    const auto before_translate = node->Translate();
    const auto before_size      = scene->runtime->NodeSize("caption");

    ASSERT_TRUE(scene->runtime->SetNodeText("caption", "aaaaaaaaaa"));

    const auto after_translate = node->Translate();
    const auto after_size      = scene->runtime->NodeSize("caption");
    EXPECT_GT(after_size.x(), before_size.x());
    EXPECT_LT(after_translate.x(), before_translate.x());
    EXPECT_FLOAT_EQ(after_translate.y(), before_translate.y());
    EXPECT_NEAR(after_translate.x(), 100.0f - after_size.x() * 0.5f, 1.0e-4f);
    EXPECT_NEAR(after_translate.y(), 50.0f + after_size.y() * 0.5f, 1.0e-4f);
}

TEST(TextObjectRuntime, ScaledTextAnchoringUsesRenderedSizeFromOriginalOrigin) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-scaled-reanchor",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "caption", "text": "aa", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "scale": [2, 3, 1], "horizontalalign": "right", "verticalalign": "bottom", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);

    const auto size = scene->runtime->NodeSize("caption");
    EXPECT_NEAR(node->Translate().x(), 100.0f - size.x(), 1.0e-4f);
    EXPECT_NEAR(node->Translate().y(), 50.0f + size.y() * 1.5f, 1.0e-4f);

    ASSERT_TRUE(scene->runtime->SetNodeText("caption", "aaaaaa"));
    const auto resized = scene->runtime->NodeSize("caption");
    EXPECT_NEAR(node->Translate().x(), 100.0f - resized.x(), 1.0e-4f);
    EXPECT_NEAR(node->Translate().y(), 50.0f + resized.y() * 1.5f, 1.0e-4f);
}

TEST(TextObjectRuntime, PumpTextLayerCacheClearsDirtyStateWithoutLosingText) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(runtime, nullptr);

    auto node = std::make_shared<SceneNode>();
    runtime->RegisterNode("caption", node.get());
    runtime->RegisterTextLayer("caption",
                               TextLayerState {
                                   .text       = "before",
                                   .font_key   = "Arial",
                                   .point_size = 12.0f,
                               });

    ASSERT_TRUE(runtime->SetNodeText("caption", "after"));
    ASSERT_TRUE(runtime->NodeTextDirty("caption"));
    runtime->PumpTextLayerCache();

    const auto state = runtime->NodeTextState("caption");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->text, "after");
    EXPECT_FALSE(state->cache_dirty);
    EXPECT_FALSE(state->full_dirty);
    EXPECT_FALSE(runtime->NodeTextDirty("caption"));
}

TEST(TextObjectRuntime, PumpTextLayerCacheDoesNotKeepDirtyWhenSceneCannotAcceptRuntimeTexture) {
    Scene scene;
    scene.runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(scene.runtime, nullptr);
    scene.runtime->AttachScene(&scene);

    auto node = std::make_shared<SceneNode>();
    scene.runtime->RegisterNode("caption", node.get());
    scene.runtime->RegisterTextLayer("caption",
                                     TextLayerState {
                                         .text       = "before",
                                         .font_key   = "Arial",
                                         .point_size = 12.0f,
                                     });

    ASSERT_TRUE(scene.runtime->SetNodeText("caption", "after"));
    ASSERT_TRUE(scene.runtime->NodeTextDirty("caption"));

    scene.runtime->PumpTextLayerCache();

    const auto state = scene.runtime->NodeTextState("caption");
    ASSERT_TRUE(state.has_value());
    EXPECT_FALSE(state->cache_dirty);
    EXPECT_FALSE(state->full_dirty);
    EXPECT_FALSE(scene.runtime->NodeTextDirty("caption"));
    EXPECT_FALSE(scene.runtime->ConsumeSceneGraphMutationFlag());
}

TEST(TextObjectRuntime, PumpTextLayerCacheRerasterizesAttachedSceneTexture) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-rerasterize",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "caption", "text": "a", "font": "Arial", "pointsize": 20, "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    ASSERT_NE(scene->runtime, nullptr);
    auto* node = FindRootChild(*scene, "caption");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->Mesh(), nullptr);
    auto* material = node->Mesh()->MaterialForSlot(0);
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->textures.size(), 1u);
    const auto texture_name = material->textures.front();

    auto before = scene->imageParser->Parse(texture_name);
    ASSERT_NE(before, nullptr);
    ASSERT_TRUE(scene->runtime->SetNodeText("caption", "aaaaaa"));
    ASSERT_TRUE(scene->runtime->NodeTextDirty("caption"));

    scene->runtime->PumpTextLayerCache();

    auto after = scene->imageParser->Parse(texture_name);
    ASSERT_NE(after, nullptr);
    EXPECT_NE(before->key, after->key);
    EXPECT_GT(after->header.width, before->header.width);
    EXPECT_FALSE(scene->runtime->NodeTextDirty("caption"));
    EXPECT_FALSE(scene->runtime->ConsumeSceneGraphMutationFlag());
}

TEST(TextObjectRuntime, TextCacheKeysAreUniquePerLayerEvenWithSameFontAndSize) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(runtime, nullptr);

    auto first  = std::make_shared<SceneNode>();
    auto second = std::make_shared<SceneNode>();
    runtime->RegisterNode("first", first.get());
    runtime->RegisterNode("second", second.get());
    runtime->RegisterTextLayer("first",
                               TextLayerState {
                                   .text       = "one",
                                   .font_key   = "Arial",
                                   .point_size = 12.0f,
                               });
    runtime->RegisterTextLayer("second",
                               TextLayerState {
                                   .text       = "two",
                                   .font_key   = "Arial",
                                   .point_size = 12.0f,
                               });

    const auto first_state  = runtime->NodeTextState("first");
    const auto second_state = runtime->NodeTextState("second");
    ASSERT_TRUE(first_state.has_value());
    ASSERT_TRUE(second_state.has_value());
    EXPECT_NE(first_state->texture_cache_key, second_state->texture_cache_key);
}

TEST(TextObjectRuntime, HorizontalAlignOverridesLegacyAlignment) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-align-horizontal",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "fallback", "text": "aaaa", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "alignment": "left", "visible": true},
          {"id": 2, "name": "override", "text": "aaaa", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "alignment": "left", "horizontalalign": "right", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    auto* fallback = FindRootChild(*scene, "fallback");
    auto* override = FindRootChild(*scene, "override");
    ASSERT_NE(fallback, nullptr);
    ASSERT_NE(override, nullptr);
    EXPECT_GT(fallback->Translate().x(), 100.0f);
    EXPECT_LT(override->Translate().x(), 100.0f);
    EXPECT_FLOAT_EQ(fallback->Translate().y(), 50.0f);
    EXPECT_FLOAT_EQ(override->Translate().y(), 50.0f);
}

TEST(TextObjectRuntime, VerticalAlignOverridesLegacyAlignment) {
    fs::VFS vfs;
    MountAssets(vfs);
    audio::SoundManager sound_manager;
    WPSceneParser       parser;
    ProjectProperties   properties;
    SceneParseRequest   request {
          .scene_id           = "text-align-vertical",
          .project_properties = &properties,
    };

    auto scene = parser.Parse(request,
                              MinimalSceneObjects(R"([
          {"id": 1, "name": "fallback", "text": "aaaa", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "alignment": "top", "visible": true},
          {"id": 2, "name": "override", "text": "aaaa", "font": "Arial", "pointsize": 10, "origin": [100, 50, 0], "alignment": "top", "verticalalign": "bottom", "visible": true}
        ])"),
                              vfs,
                              sound_manager);

    ASSERT_NE(scene, nullptr);
    auto* fallback = FindRootChild(*scene, "fallback");
    auto* override = FindRootChild(*scene, "override");
    ASSERT_NE(fallback, nullptr);
    ASSERT_NE(override, nullptr);
    EXPECT_LT(fallback->Translate().y(), 50.0f);
    EXPECT_GT(override->Translate().y(), 50.0f);
    EXPECT_FLOAT_EQ(fallback->Translate().x(), 100.0f);
    EXPECT_FLOAT_EQ(override->Translate().x(), 100.0f);
}

TEST(TextObjectRuntime, ScriptThisLayerTextSetterMutatesRuntimeText) {
    auto runtime = CreateSceneRuntimeContext(SceneRuntimeBootstrap {});
    ASSERT_NE(runtime, nullptr);

    auto node = std::make_shared<SceneNode>();
    runtime->RegisterNode("caption", node.get());
    runtime->RegisterTextLayer("caption",
                               TextLayerState {
                                   .text       = "before",
                                   .font_key   = "Arial",
                                   .point_size = 12.0f,
                                   .padding    = 0.0f,
                               });

    auto program = runtime->scriptEngine().CreatePropertyScriptProgram(runtime.get(),
                                                                       R"JS(
export function update(value) {
  thisLayer.text = "after";
  var indirect = thisScene.getLayer('caption');
  indirect.text = indirect.text + " indirect";
  return thisLayer.text === "after indirect" ? 1 : -1;
}
)JS",
                                                                       "caption",
                                                                       {},
                                                                       DynamicValue(0.0f),
                                                                       runtime->hostContext());
    ASSERT_NE(program, nullptr);

    const auto result = program->Evaluate(runtime->hostContext(), DynamicValue(0.0f));
    ASSERT_NE(result, nullptr);
    EXPECT_FLOAT_EQ(result->getFloat(), 1.0f);
    EXPECT_EQ(runtime->NodeText("caption"), "after indirect");
    EXPECT_EQ(runtime->scriptErrorCount(), 0u);
}

TEST(TextObjectRuntime, ResolvesWallpaperEngineSansSerifSystemFontAlias) {
#ifdef __APPLE__
    EXPECT_FALSE(ResolveSystemFontPath("systemfont_sansserif").empty());
#endif
}

TEST(TextObjectRuntime, FreeTypeRasterizationFallsBackForMissingPrimaryGlyph) {
#ifdef __APPLE__
    TextLayerState state {
        .text                   = "\xE4\xB8\xAD",
        .font_key               = "systemfont_Helvetica",
        .resolved_font_kind     = "system",
        .resolved_font_identity = "Helvetica",
        .resolved_font_path     = ResolveSystemFontPath("systemfont_Helvetica"),
        .point_size             = 40.0f,
        .padding                = 4.0f,
    };
    ASSERT_FALSE(state.resolved_font_path.empty());

    const auto size = TextLayerRasterSize(state);
    EXPECT_GT(size.x(), 40.0f);
    EXPECT_GT(size.y(), 40.0f);

    const auto width  = static_cast<uint32_t>(std::ceil(size.x()));
    const auto height = static_cast<uint32_t>(std::ceil(size.y()));
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u, 0u);
    RasterizeTextLayer(state, width, height, rgba);

    TextLayerState unsupported_state = state;
    unsupported_state.text           = "\xF4\x8F\xBF\xBF";
    const auto unsupported_size      = TextLayerRasterSize(unsupported_state);
    const auto unsupported_width     = static_cast<uint32_t>(std::ceil(unsupported_size.x()));
    const auto unsupported_height    = static_cast<uint32_t>(std::ceil(unsupported_size.y()));
    std::vector<uint8_t> unsupported_rgba(static_cast<std::size_t>(unsupported_width) *
                                              unsupported_height * 4u,
                                          0u);
    RasterizeTextLayer(
        unsupported_state, unsupported_width, unsupported_height, unsupported_rgba);

    int min_x = static_cast<int>(width);
    int max_x = -1;
    int min_y = static_cast<int>(height);
    int max_y = -1;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto alpha = rgba[(static_cast<std::size_t>(y) * width + x) * 4u + 3u];
            if (alpha == 0u) continue;
            min_x = std::min(min_x, static_cast<int>(x));
            max_x = std::max(max_x, static_cast<int>(x));
            min_y = std::min(min_y, static_cast<int>(y));
            max_y = std::max(max_y, static_cast<int>(y));
        }
    }

    ASSERT_GE(max_x, min_x);
    ASSERT_GE(max_y, min_y);
    EXPECT_GT(max_x - min_x + 1, 20);
    EXPECT_GT(max_y - min_y + 1, 20);
    EXPECT_NE(rgba, unsupported_rgba);
#endif
}

} // namespace wallpaper
