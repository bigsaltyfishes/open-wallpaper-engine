#include "Interface/IImageParser.h"
#include "Runtime/RuntimeImageSource.hpp"
#include "WPSceneParser.hpp"
#include "wpscene/WPMaterial.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace wallpaper
{
namespace
{

class NullImageParser final : public IImageParser {
public:
    std::shared_ptr<Image> Parse(const std::string&) override { return nullptr; }
    ImageHeader            ParseHeader(const std::string&) override { return {}; }
};

TEST(MediaThumbnailTextureSmoke, RegularUserTextureSlotZeroResolvesToMediaThumbnail) {
    const nlohmann::json material_json = {
        { "passes",
          { {
              { "shader", "default" },
              { "textures", { "materials/default.tex" } },
              { "usertextures", { { { "type", "system" }, { "name", "$mediaThumbnail" } } } },
          } } },
    };

    wpscene::WPMaterial material;
    ASSERT_TRUE(material.FromJson(material_json));
    ASSERT_EQ(material.textures.size(), 1u);
    ASSERT_EQ(material.usertextures.size(), 1u);

    auto textures = material.textures;
    ApplySystemUserTextures(textures, material.usertextures);

    ASSERT_EQ(textures.size(), 1u);
    EXPECT_EQ(textures[0], "$mediaThumbnail");
}

TEST(MediaThumbnailTextureSmoke, EffectUserTextureSlotOneResolvesToMediaThumbnail) {
    const nlohmann::json material_json = {
        { "passes",
          { {
              { "shader", "default" },
              { "textures", { "materials/base.tex", "materials/placeholder.tex" } },
          } } },
    };
    const nlohmann::json effect_pass_json = {
        { "textures", { "materials/base.tex", "materials/effect-placeholder.tex" } },
        { "usertextures", { nullptr, { { "type", "system" }, { "name", "$mediaThumbnail" } } } },
    };

    wpscene::WPMaterial material;
    ASSERT_TRUE(material.FromJson(material_json));

    wpscene::WPMaterialPass effect_pass;
    ASSERT_TRUE(effect_pass.FromJson(effect_pass_json));
    material.MergePass(effect_pass);

    ASSERT_EQ(material.textures.size(), 2u);
    ASSERT_EQ(material.usertextures.size(), 2u);

    auto textures = material.textures;
    ApplySystemUserTextures(textures, material.usertextures);

    ASSERT_EQ(textures.size(), 2u);
    EXPECT_EQ(textures[0], "materials/base.tex");
    EXPECT_EQ(textures[1], "$mediaThumbnail");
}

TEST(MediaThumbnailTextureSmoke, RuntimeImageSourceStoresExactRgbaPayload) {
    RuntimeImageSource         source(std::make_unique<NullImageParser>());
    const std::vector<uint8_t> rgba {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    };

    source.SetRgbaImage("$mediaThumbnail", 2, 1, rgba.data(), rgba.size());

    const auto header = source.ParseHeader("$mediaThumbnail");
    EXPECT_EQ(header.width, 2);
    EXPECT_EQ(header.height, 1);
    EXPECT_EQ(header.mapWidth, 2);
    EXPECT_EQ(header.mapHeight, 1);
    EXPECT_EQ(header.count, 1);
    EXPECT_EQ(header.format, TextureFormat::RGBA8);

    const auto image = source.Parse("$mediaThumbnail");
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->header.width, 2);
    EXPECT_EQ(image->header.height, 1);
    ASSERT_EQ(image->slots.size(), 1u);
    EXPECT_EQ(image->slots[0].width, 2);
    EXPECT_EQ(image->slots[0].height, 1);
    ASSERT_EQ(image->slots[0].mipmaps.size(), 1u);

    const auto& mip = image->slots[0].mipmaps[0];
    EXPECT_EQ(mip.width, 2);
    EXPECT_EQ(mip.height, 1);
    ASSERT_EQ(mip.size, static_cast<isize>(rgba.size()));
    ASSERT_NE(mip.data, nullptr);
    EXPECT_EQ(std::memcmp(mip.data.get(), rgba.data(), rgba.size()), 0);
}

} // namespace
} // namespace wallpaper
