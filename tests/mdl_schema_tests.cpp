#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Fs/Fs.h"
#include "Fs/MemBinaryStream.h"
#include "Fs/VFS.h"
#include "WPMdlParser.hpp"

namespace
{
using namespace wallpaper;

class MemoryFs final : public fs::Fs {
public:
    explicit MemoryFs(std::map<std::string, std::vector<uint8_t>> files)
        : m_files(std::move(files)) {}

    bool Contains(std::string_view path) const override {
        return m_files.contains(std::string(path));
    }

    std::shared_ptr<fs::IBinaryStream> Open(std::string_view path) override {
        const auto it = m_files.find(std::string(path));
        if (it == m_files.end()) return nullptr;
        return std::make_shared<fs::MemBinaryStream>(std::vector<uint8_t>(it->second));
    }

    std::shared_ptr<fs::IBinaryStreamW> OpenW(std::string_view) override { return nullptr; }

private:
    std::map<std::string, std::vector<uint8_t>> m_files;
};

class Bytes {
public:
    void Stamp(std::string_view prefix, int version) {
        char stamp[9] {};
        std::snprintf(stamp, sizeof(stamp), "%.4s%.4d", prefix.data(), version);
        Raw(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(stamp), sizeof(stamp)));
    }
    void Str(std::string_view value) {
        Raw(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data()), value.size()));
        U8(0);
    }
    void U8(uint8_t value) { RawValue(value); }
    void U16(uint16_t value) { RawValue(value); }
    void U32(uint32_t value) { RawValue(value); }
    void I32(int32_t value) { RawValue(value); }
    void F32(float value) { RawValue(value); }

    std::vector<uint8_t> Take() { return std::move(m_bytes); }

private:
    template<typename T>
    void RawValue(const T& value) {
        Raw(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
    }
    void Raw(std::span<const uint8_t> bytes) {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    std::vector<uint8_t> m_bytes;
};

constexpr uint32_t kSkinUvFlag = 0x00800000u | 0x01000000u | 0x00000008u;

void WriteVertex(Bytes& b, float x, float y, float u, uint32_t bone = 0) {
    b.F32(x);
    b.F32(y);
    b.F32(0.0f);
    b.U32(bone);
    b.U32(0);
    b.U32(0);
    b.U32(0);
    b.F32(1.0f);
    b.F32(0.0f);
    b.F32(0.0f);
    b.F32(0.0f);
    b.F32(u);
    b.F32(0.5f);
}

void WriteMesh(Bytes& b, std::string_view material, uint32_t part_id) {
    b.Str(material);
    b.U32(0);
    b.F32(-1.0f);
    b.F32(-1.0f);
    b.F32(0.0f);
    b.F32(1.0f);
    b.F32(1.0f);
    b.F32(0.0f);
    b.U32(kSkinUvFlag);
    b.U32(3u * 52u);
    WriteVertex(b, 0.0f, 0.0f, 0.0f);
    WriteVertex(b, 1.0f, 0.0f, 0.5f);
    WriteVertex(b, 0.0f, 1.0f, 1.0f);
    b.U32(6);
    b.U16(0);
    b.U16(1);
    b.U16(2);
    b.U8(1);
    b.U8(1);
    b.U16(0);
    b.U8(0);
    b.U32(36);
    for (uint32_t i = 0; i < 3; ++i) {
        b.F32(0.25f * static_cast<float>(i));
        b.F32(0.5f);
        b.U32(0);
    }
    b.U8(1);
    b.U32(16);
    b.U32(part_id);
    b.U32(0);
    b.U32(0);
    b.U32(3);
}

void WriteIdentity3x4(Bytes& b) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            b.F32(row == col ? 1.0f : 0.0f);
        }
    }
}

void WriteTranslate3x4(Bytes& b, float x, float y, float z) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float value = row == col ? 1.0f : 0.0f;
            if (col == 3 && row == 0) value = x;
            if (col == 3 && row == 1) value = y;
            if (col == 3 && row == 2) value = z;
            b.F32(value);
        }
    }
}

std::vector<uint8_t> BuildMdlv21TwoMeshFixture() {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(2);
    WriteMesh(b, "mat/head.json", 10);
    WriteMesh(b, "mat/eyes.json", 20);

    b.Stamp("MDLS", 1);
    b.U32(0);
    b.U16(4);
    b.U16(0);
    b.Str("root");
    b.I32(0);
    b.U32(0xFFFFFFFFu);
    b.U32(64);
    WriteIdentity3x4(b);
    b.Str("{}");
    b.Str("first_child");
    b.I32(0);
    b.U32(0);
    b.U32(64);
    WriteIdentity3x4(b);
    b.Str("{}");
    b.Str("second_child");
    b.I32(0);
    b.U32(0);
    b.U32(64);
    WriteIdentity3x4(b);
    b.Str("{}");
    b.Str("grandchild");
    b.I32(0);
    b.U32(1);
    b.U32(64);
    WriteIdentity3x4(b);
    b.Str("{}");

    b.Stamp("MDLA", 0);
    b.U8(0);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv21TranslatedBonesFixture() {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(1);
    WriteMesh(b, "mat/head.json", 10);

    b.Stamp("MDLS", 1);
    b.U32(0);
    b.U16(3);
    b.U16(0);
    b.Str("root");
    b.I32(0);
    b.U32(0xFFFFFFFFu);
    b.U32(64);
    WriteTranslate3x4(b, 10.0f, 0.0f, 0.0f);
    b.Str("{}");
    b.Str("first_child");
    b.I32(0);
    b.U32(0);
    b.U32(64);
    WriteTranslate3x4(b, 3.0f, 0.0f, 0.0f);
    b.Str("{}");
    b.Str("grandchild");
    b.I32(0);
    b.U32(1);
    b.U32(64);
    WriteTranslate3x4(b, 2.0f, 0.0f, 0.0f);
    b.Str("{}");

    b.Stamp("MDLA", 0);
    b.U8(0);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv21WithMeshCount(uint32_t mesh_count) {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(mesh_count);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv20LegacyHeaderWithMeshCountTwo() {
    Bytes b;
    b.Stamp("MDL", 20);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(2);
    b.Str("legacy.json");
    b.U32(0);
    b.U32(52);
    WriteVertex(b, 0.0f, 0.0f, 0.0f);
    b.U32(0);
    b.Stamp("MDLS", 1);
    b.U32(0);
    b.U16(1);
    b.U16(0);
    b.Str("root");
    b.I32(0);
    b.U32(0xFFFFFFFFu);
    b.U32(64);
    WriteIdentity3x4(b);
    b.Str("{}");
    b.Stamp("MDLA", 0);
    b.U8(0);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv21WithTruncatedVertexPayload() {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(1);
    b.Str("mat/head.json");
    b.U32(0);
    b.F32(-1.0f);
    b.F32(-1.0f);
    b.F32(0.0f);
    b.F32(1.0f);
    b.F32(1.0f);
    b.F32(0.0f);
    b.U32(kSkinUvFlag);
    b.U32(52);
    b.F32(0.0f);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv21WithTruncatedPartsPayload() {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(1);
    b.Str("mat/head.json");
    b.U32(0);
    b.F32(-1.0f);
    b.F32(-1.0f);
    b.F32(0.0f);
    b.F32(1.0f);
    b.F32(1.0f);
    b.F32(0.0f);
    b.U32(kSkinUvFlag);
    b.U32(52);
    WriteVertex(b, 0.0f, 0.0f, 0.0f);
    b.U32(0);
    b.U8(1);
    b.U8(1);
    b.U16(0);
    b.U8(0);
    b.U32(12);
    b.F32(0.25f);
    return b.Take();
}

std::vector<uint8_t> BuildMdlv21WithInvalidPartsBytes() {
    Bytes b;
    b.Stamp("MDL", 21);
    b.U32(kSkinUvFlag);
    b.U32(1);
    b.U32(1);
    b.Str("mat/head.json");
    b.U32(0);
    b.F32(-1.0f);
    b.F32(-1.0f);
    b.F32(0.0f);
    b.F32(1.0f);
    b.F32(1.0f);
    b.F32(0.0f);
    b.U32(kSkinUvFlag);
    b.U32(52);
    WriteVertex(b, 0.0f, 0.0f, 0.0f);
    b.U32(0);
    b.U8(0);
    b.U8(1);
    b.U32(15);
    return b.Take();
}

void MountMdlFixture(fs::VFS& vfs, std::vector<uint8_t> bytes) {
    auto files = std::map<std::string, std::vector<uint8_t>> {
        { "/sample.mdl", std::move(bytes) },
    };
    EXPECT_TRUE(vfs.Mount("/assets", std::make_unique<MemoryFs>(std::move(files))));
}

} // namespace

TEST(MdlSchema, ParsesMdlv21PartsBeforeMdlsAndMultipleMeshes) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21TwoMeshFixture());
    WPMdl mdl;

    ASSERT_TRUE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
    EXPECT_EQ(mdl.mdlv, 21);
    EXPECT_EQ(mdl.mdl_header.mesh_count, 2u);
    ASSERT_EQ(mdl.meshes.size(), 2u);
    EXPECT_EQ(mdl.meshes[0].mat_json_file, "mat/head.json");
    EXPECT_EQ(mdl.meshes[1].mat_json_file, "mat/eyes.json");
    ASSERT_EQ(mdl.meshes[0].part_uv2.size(), 3u);
    ASSERT_EQ(mdl.meshes[0].parts.size(), 1u);
    EXPECT_EQ(mdl.meshes[0].parts[0].id, 10u);
    EXPECT_EQ(mdl.meshes[1].parts[0].id, 20u);
    EXPECT_EQ(mdl.mat_json_file, "mat/head.json");
    EXPECT_EQ(mdl.vertexs.size(), 3u);
}

TEST(MdlSchema, PreservesFirstLevelBoneHierarchyForPuppetEvidence) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21TwoMeshFixture());
    WPMdl mdl;

    ASSERT_TRUE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
    ASSERT_NE(mdl.puppet, nullptr);
    ASSERT_EQ(mdl.puppet->bones.size(), 4u);
    EXPECT_TRUE(mdl.puppet->bones[0].noParent());
    EXPECT_EQ(mdl.puppet->bones[1].parent, 0u);
    EXPECT_EQ(mdl.puppet->bones[2].parent, 0u);
    EXPECT_EQ(mdl.puppet->bones[3].parent, 1u);
    EXPECT_EQ(mdl.puppet->bones[1].bind_parent, 0xFFFFFFFFu);
    EXPECT_EQ(mdl.puppet->bones[2].bind_parent, 0xFFFFFFFFu);
    EXPECT_EQ(mdl.puppet->bones[3].bind_parent, 1u);
}

TEST(MdlSchema, UsesFlattenedBindHierarchyForGeneratedPuppetOffsets) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21TranslatedBonesFixture());
    WPMdl mdl;

    ASSERT_TRUE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
    ASSERT_NE(mdl.puppet, nullptr);

    WPPuppetLayer layer(mdl.puppet);
    std::vector<WPPuppetLayer::AnimationLayer> layers;
    layer.prepared(layers);
    const auto frame = layer.genFrame(0.0);

    ASSERT_EQ(frame.size(), 3u);
    EXPECT_NEAR(frame[0].translation().x(), 0.0f, 1.0e-5f);
    EXPECT_NEAR(frame[1].translation().x(), 10.0f, 1.0e-5f);
    EXPECT_NEAR(frame[2].translation().x(), 10.0f, 1.0e-5f);
}

TEST(MdlSchema, RejectsOversizedMdlv21MeshCountBeforeAllocation) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21WithMeshCount(100000));
    WPMdl mdl;

    EXPECT_FALSE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
}

TEST(MdlSchema, RejectsTruncatedMdlv21VertexPayload) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21WithTruncatedVertexPayload());
    WPMdl mdl;

    EXPECT_FALSE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
}

TEST(MdlSchema, RejectsTruncatedMdlv21PartsPayload) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21WithTruncatedPartsPayload());
    WPMdl mdl;

    EXPECT_FALSE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
}

TEST(MdlSchema, RejectsInvalidMdlv21PartsBytes) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv21WithInvalidPartsBytes());
    WPMdl mdl;

    EXPECT_FALSE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
}

TEST(MdlSchema, MeshCountGreaterThanOneDoesNotSelectMdlv21PathBeforeMdlv21) {
    fs::VFS vfs;
    MountMdlFixture(vfs, BuildMdlv20LegacyHeaderWithMeshCountTwo());
    WPMdl mdl;

    ASSERT_TRUE(WPMdlParser::Parse("sample.mdl", vfs, mdl));
    EXPECT_EQ(mdl.mdlv, 20);
    EXPECT_TRUE(mdl.meshes.empty());
    EXPECT_EQ(mdl.mat_json_file, "legacy.json");
}
