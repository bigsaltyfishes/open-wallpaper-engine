#pragma once
#include <string>
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <Eigen/Dense>

#include "WPPuppet.hpp"

namespace wallpaper
{

class WPShaderInfo;

namespace wpscene
{
class WPMaterial;
};
namespace fs
{
class VFS;
};

struct WPMdl {
    struct Header {
        i32      mdlv { 13 };
        uint32_t mdl_flag { 0 };
        uint32_t mesh_count { 1 };
    };
    struct Mesh {
        std::string mat_json_file;
        uint32_t    flag_a { 0 };
        uint32_t    flag { 0 };
        std::array<float, 3> aabb_min {};
        std::array<float, 3> aabb_max {};
        bool        has_aabb { false };

        std::vector<std::array<float, 3>>    positions;
        std::vector<std::array<float, 3>>    normals;
        std::vector<std::array<float, 4>>    tangents;
        std::vector<std::array<uint8_t, 4>>  extra4;
        std::vector<std::array<uint32_t, 4>> blend_indices;
        std::vector<std::array<float, 4>>    blend_weights;
        std::vector<std::array<float, 2>>    texcoords;
        std::vector<std::array<float, 2>>    texcoord2;
        std::vector<std::array<uint16_t, 3>> indices;

        struct Part {
            uint32_t id { 0 };
            uint32_t start { 0 };
            uint32_t size { 0 };
        };
        std::vector<std::array<float, 2>> part_uv2;
        std::vector<uint32_t>             part_uv2_pad;
        std::vector<Part>                 parts;
    };

    Header mdl_header;
    std::vector<Mesh> meshes;
    i32 mdlv { 13 };
    i32 mdls { 1 };
    i32 mdla { 1 };

    std::string mat_json_file;
    struct Vertex {
        std::array<float, 3>    position;
        std::array<uint32_t, 4> blend_indices;
        std::array<float, 4>    weight;
        std::array<float, 2>    texcoord;
    };
    std::vector<Vertex>                  vertexs;
    std::vector<std::array<uint16_t, 3>> indices;

    // std::vector<Eigen::Matrix<float, 3, 4>> bones;
    std::shared_ptr<WPPuppet> puppet;
    // combo
    // SKINNING = 1
    // BONECOUNT

    // input
    // uvec4 a_BlendIndices
    // vec4 a_BlendWeights
    // uniform mat4x3 g_Bones[BONECOUNT]
};

class SceneMesh;

class WPMdlParser {
public:
    static bool Parse(std::string_view path, fs::VFS&, WPMdl&);

    static void AddPuppetShaderInfo(WPShaderInfo& info, const WPMdl& mdl);
    static void AddPuppetMatInfo(wpscene::WPMaterial& mat, const WPMdl& mdl);

    static void GenPuppetMesh(SceneMesh& mesh, const WPMdl& mdl);
};

} // namespace wallpaper
