#include "Fs/PhysicalFs.h"
#include "Fs/VFS.h"
#include "WPShaderParser.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace wallpaper
{
namespace
{

class GlslangEnvironment final {
public:
    GlslangEnvironment() { WPShaderParser::InitGlslang(); }
    ~GlslangEnvironment() { WPShaderParser::FinalGlslang(); }
};

std::filesystem::path MakeTempDir() {
    auto root = std::filesystem::temp_directory_path() / "wpe-shader-cache-metadata-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "cache", ec);
    std::filesystem::create_directories(root / "assets", ec);
    return root;
}

std::array<WPShaderUnit, 2> MakeShaderUnits(fs::VFS& vfs, WPShaderInfo& info) {
    std::array<WPShaderUnit, 2> units {
        WPShaderUnit {
            .stage           = ShaderType::VERTEX,
            .src             = R"(
in vec3 a_Position;
out vec2 v_TexCoord;
void main() {
    v_TexCoord = vec2(0.5, 0.5);
    gl_Position = vec4(a_Position, 1.0);
}
)",
            .preprocess_info = {},
        },
        WPShaderUnit {
            .stage           = ShaderType::FRAGMENT,
            .src             = R"(
in vec2 v_TexCoord;
uniform sampler2D g_Texture0;
void main() {
    gl_FragColor = texture(g_Texture0, v_TexCoord);
}
)",
            .preprocess_info = {},
        },
    };

    std::vector<WPShaderTexInfo> textures { WPShaderTexInfo { .enabled = true } };
    for (auto& unit : units) {
        unit.src = WPShaderParser::PreShaderSrc(vfs, unit.src, &info, textures);
    }
    return units;
}

TEST(ShaderCacheMetadataTest, CacheHitStillPopulatesActiveTextureSlots) {
    GlslangEnvironment glslang;
    const auto         temp = MakeTempDir();

    fs::VFS vfs;
    ASSERT_TRUE(
        vfs.Mount("/cache", fs::CreatePhysicalFs((temp / "cache").string(), true), "cache"));
    ASSERT_TRUE(
        vfs.Mount("/assets", fs::CreatePhysicalFs((temp / "assets").string(), true), "assets"));

    std::vector<WPShaderTexInfo> textures { WPShaderTexInfo { .enabled = true } };

    {
        WPShaderInfo            info;
        auto                    units = MakeShaderUnits(vfs, info);
        std::vector<ShaderCode> codes;
        ASSERT_TRUE(WPShaderParser::CompileToSpv("demo-scene", units, codes, vfs, &info, textures));
        ASSERT_TRUE(units[1].preprocess_info.active_tex_slots.contains(0));
    }

    {
        WPShaderInfo            info;
        auto                    units = MakeShaderUnits(vfs, info);
        std::vector<ShaderCode> codes;
        ASSERT_TRUE(WPShaderParser::CompileToSpv("demo-scene", units, codes, vfs, &info, textures));
        ASSERT_TRUE(units[1].preprocess_info.active_tex_slots.contains(0));
    }
}

} // namespace
} // namespace wallpaper
