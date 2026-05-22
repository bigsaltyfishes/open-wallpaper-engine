#include "WPMdlParser.hpp"
#include "Fs/VFS.h"
#include "Fs/IBinaryStream.h"
#include "Fs/MemBinaryStream.h"
#include "WPCommon.hpp"
#include "Utils/Logging.h"
#include "Scene/SceneMesh.h"
#include "SpecTexs.hpp"
#include "wpscene/WPMaterial.h"
#include "WPShaderParser.hpp"

#include <cstring>

using namespace wallpaper;

namespace
{
constexpr uint32_t kIndexTriangleBytes = 2 * 3;
constexpr uint32_t kMaxMdlMeshes       = 64;
constexpr uint32_t kMaxMdlVertices     = 1'000'000;
constexpr uint32_t kMaxMdlTriangles    = 2'000'000;
constexpr uint32_t kMaxMdlParts        = 1'000'000;
constexpr uint32_t MDL_FLAG_NORMAL      = 0x00000002;
constexpr uint32_t MDL_FLAG_TANGENT     = 0x00000004;
constexpr uint32_t MDL_FLAG_UV          = 0x00000008;
constexpr uint32_t MDL_FLAG_UV2         = 0x00000020;
constexpr uint32_t MDL_FLAG_EXTRA4      = 0x00010000;
constexpr uint32_t MDL_FLAG_SKIN_BLEND  = 0x00800000;
constexpr uint32_t MDL_FLAG_SKIN_WEIGHT = 0x01000000;

WPPuppet::PlayMode ToPlayMode(std::string_view m) {
    if (m == "loop" || m.empty()) return WPPuppet::PlayMode::Loop;
    if (m == "mirror") return WPPuppet::PlayMode::Mirror;
    if (m == "single") return WPPuppet::PlayMode::Single;

    LOG_INFO("unknown puppet animation play mode \"%s\"", m.data());
    assert(m == "loop");
    return WPPuppet::PlayMode::Loop;
}

bool PeekBlockMagic(fs::MemBinaryStream& f, std::string_view expect4) {
    if (expect4.size() != 4) return false;
    const auto save = f.Tell();
    if (save + 4 > f.Size()) return false;
    char buf[4] {};
    f.Read(buf, 4);
    f.SeekSet(save);
    return std::memcmp(buf, expect4.data(), 4) == 0;
}

bool HasRemaining(const fs::MemBinaryStream& f, uint64_t bytes) {
    const auto pos  = f.Tell();
    const auto size = f.Size();
    return pos >= 0 && size >= pos && bytes <= static_cast<uint64_t>(size - pos);
}

bool ReadCStringBounded(fs::MemBinaryStream& f, std::string& out) {
    out.clear();
    while (HasRemaining(f, 1)) {
        const char c = static_cast<char>(f.ReadUint8());
        if (c == '\0') return true;
        out.push_back(c);
    }
    return false;
}

uint32_t ComputeVertexStride(uint32_t flag) {
    uint32_t stride = 12;
    if (flag & MDL_FLAG_NORMAL) stride += 12;
    if (flag & MDL_FLAG_TANGENT) stride += 16;
    if (flag & MDL_FLAG_EXTRA4) stride += 4;
    if (flag & MDL_FLAG_SKIN_BLEND) stride += 16;
    if (flag & MDL_FLAG_SKIN_WEIGHT) stride += 16;
    if (flag & MDL_FLAG_UV) stride += 8;
    if (flag & MDL_FLAG_UV2) stride += 8;
    return stride;
}

bool ParseMesh(fs::MemBinaryStream& f, const WPMdl::Header& header, WPMdl::Mesh& mesh,
               std::string_view path) {
    if (! ReadCStringBounded(f, mesh.mat_json_file) || ! HasRemaining(f, 4)) {
        LOG_INFO("truncated mdl mesh header in %s", std::string(path).c_str());
        return false;
    }
    mesh.flag_a        = f.ReadUint32();
    if (mesh.flag_a == 2) {
        if (! HasRemaining(f, 4)) return false;
        (void)f.ReadUint32();
    }

    if (header.mdlv >= 17) {
        if (! HasRemaining(f, 24)) return false;
        for (auto& v : mesh.aabb_min) v = f.ReadFloat();
        for (auto& v : mesh.aabb_max) v = f.ReadFloat();
        mesh.has_aabb = true;
    }

    if (! HasRemaining(f, header.mdlv > 14 ? 8 : 4)) return false;
    mesh.flag = header.mdlv > 14 ? f.ReadUint32() : header.mdl_flag;
    const uint32_t vertex_size = f.ReadUint32();
    const uint32_t stride      = ComputeVertexStride(mesh.flag);
    if (stride == 0 || vertex_size % stride != 0) {
        LOG_INFO("unsupport mdl vertex size %d (flag=0x%X stride=%d) in %s",
                 vertex_size, mesh.flag, stride, std::string(path).c_str());
        return false;
    }

    const uint32_t vertex_num = vertex_size / stride;
    if (vertex_num > kMaxMdlVertices || ! HasRemaining(f, vertex_size)) {
        LOG_INFO("mdlv%d vertex payload too large or truncated in %s",
                 header.mdlv, std::string(path).c_str());
        return false;
    }
    mesh.positions.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_NORMAL) mesh.normals.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_TANGENT) mesh.tangents.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_EXTRA4) mesh.extra4.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_SKIN_BLEND) mesh.blend_indices.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_SKIN_WEIGHT) mesh.blend_weights.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_UV) mesh.texcoords.resize(vertex_num);
    if (mesh.flag & MDL_FLAG_UV2) mesh.texcoord2.resize(vertex_num);

    for (uint32_t i = 0; i < vertex_num; ++i) {
        for (auto& v : mesh.positions[i]) v = f.ReadFloat();
        if (mesh.flag & MDL_FLAG_NORMAL) {
            for (auto& v : mesh.normals[i]) v = f.ReadFloat();
        }
        if (mesh.flag & MDL_FLAG_TANGENT) {
            for (auto& v : mesh.tangents[i]) v = f.ReadFloat();
        }
        if (mesh.flag & MDL_FLAG_EXTRA4) {
            for (auto& v : mesh.extra4[i]) v = f.ReadUint8();
        }
        if (mesh.flag & MDL_FLAG_SKIN_BLEND) {
            for (auto& v : mesh.blend_indices[i]) v = f.ReadUint32();
        }
        if (mesh.flag & MDL_FLAG_SKIN_WEIGHT) {
            for (auto& v : mesh.blend_weights[i]) v = f.ReadFloat();
        }
        if (mesh.flag & MDL_FLAG_UV) {
            for (auto& v : mesh.texcoords[i]) v = f.ReadFloat();
        }
        if (mesh.flag & MDL_FLAG_UV2) {
            for (auto& v : mesh.texcoord2[i]) v = f.ReadFloat();
        }
    }

    if (! HasRemaining(f, 4)) return false;
    const uint32_t indices_size = f.ReadUint32();
    if (indices_size % kIndexTriangleBytes != 0) {
        LOG_INFO("unsupport mdl indices size %d in %s", indices_size, std::string(path).c_str());
        return false;
    }
    const uint32_t index_count = indices_size / kIndexTriangleBytes;
    if (index_count > kMaxMdlTriangles || ! HasRemaining(f, indices_size)) {
        LOG_INFO("mdlv%d index payload too large or truncated in %s",
                 header.mdlv, std::string(path).c_str());
        return false;
    }
    mesh.indices.resize(index_count);
    for (auto& id : mesh.indices) {
        for (auto& v : id) v = f.ReadUint16();
    }

    if (header.mdlv >= 21) {
        if (! HasRemaining(f, 1)) return false;
        const uint8_t extras = f.ReadUint8();
        if (extras == 1) {
            if (! HasRemaining(f, 1)) return false;
            const uint8_t has_payload = f.ReadUint8();
            if (has_payload) {
                if (! HasRemaining(f, 7)) return false;
                (void)f.ReadUint16();
                (void)f.ReadUint8();
                const uint32_t payload_size = f.ReadUint32();
                if (vertex_num > UINT32_MAX / 12u || payload_size != vertex_num * 12u) {
                    LOG_INFO("mdlv%d parts payload size %d != 12*%d",
                             header.mdlv, payload_size, vertex_num);
                    return false;
                }
                if (! HasRemaining(f, payload_size)) {
                    LOG_INFO("mdlv%d parts payload truncated in %s",
                             header.mdlv, std::string(path).c_str());
                    return false;
                }
                mesh.part_uv2.resize(vertex_num);
                mesh.part_uv2_pad.resize(vertex_num);
                for (uint32_t i = 0; i < vertex_num; ++i) {
                    for (auto& v : mesh.part_uv2[i]) v = f.ReadFloat();
                    mesh.part_uv2_pad[i] = f.ReadUint32();
                }
            }
        } else if (extras != 0) {
            LOG_INFO("mdlv%d unhandled parts extras %d", header.mdlv, extras);
            return false;
        }

        if (! HasRemaining(f, 1)) return false;
        const uint8_t has_parts = f.ReadUint8();
        if (has_parts) {
            if (! HasRemaining(f, 4)) return false;
            const uint32_t parts_bytes = f.ReadUint32();
            if (parts_bytes % 16 != 0) return false;
            const uint32_t part_count = parts_bytes / 16;
            if (part_count > kMaxMdlParts || ! HasRemaining(f, parts_bytes)) {
                LOG_INFO("mdlv%d parts list too large or truncated in %s",
                         header.mdlv, std::string(path).c_str());
                return false;
            }
            mesh.parts.resize(part_count);
            for (auto& part : mesh.parts) {
                part.id = f.ReadUint32();
                (void)f.ReadUint32();
                part.start = f.ReadUint32();
                part.size = f.ReadUint32();
            }
        }
    }
    return true;
}

void MirrorFirstMeshToLegacyFields(WPMdl& mdl) {
    if (mdl.meshes.empty()) return;
    const auto& mesh = mdl.meshes.front();
    mdl.mat_json_file = mesh.mat_json_file;
    mdl.indices       = mesh.indices;
    mdl.vertexs.resize(mesh.positions.size());
    for (std::size_t i = 0; i < mdl.vertexs.size(); ++i) {
        mdl.vertexs[i].position = mesh.positions[i];
        if (i < mesh.blend_indices.size()) mdl.vertexs[i].blend_indices = mesh.blend_indices[i];
        if (i < mesh.blend_weights.size()) mdl.vertexs[i].weight = mesh.blend_weights[i];
        if (i < mesh.texcoords.size()) mdl.vertexs[i].texcoord = mesh.texcoords[i];
    }
}

SceneVertexArray MakePuppetVertexArray(const std::size_t vertex_count) {
    return SceneVertexArray({ { WE_IN_POSITION.data(), VertexType::FLOAT3 },
                              { WE_IN_BLENDINDICES.data(), VertexType::UINT4 },
                              { WE_IN_BLENDWEIGHTS.data(), VertexType::FLOAT4 },
                              { WE_IN_TEXCOORD.data(), VertexType::FLOAT2 } },
                            vertex_count);
}

std::vector<uint16_t> FlattenIndices(std::span<const std::array<uint16_t, 3>> triangles) {
    std::vector<uint16_t> indices;
    indices.reserve(triangles.size() * 3);
    for (const auto& triangle : triangles) {
        for (const uint16_t index : triangle) indices.push_back(index);
    }
    return indices;
}

SceneIndexArray MakePuppetIndexArray(std::span<const std::array<uint16_t, 3>> triangles) {
    const auto indices = FlattenIndices(triangles);
    if (indices.empty()) {
        static constexpr std::array<uint32_t, 1> kPaddedEmptyIndex { 0u };
        return SceneIndexArray(kPaddedEmptyIndex);
    }
    SceneIndexArray array(indices.size() / 3);
    array.AssignHalf(0, indices);
    return array;
}

void GenMeshFromMdl(SceneMesh::Submesh& submesh, const WPMdl::Mesh& mdl_mesh) {
    auto vertex = MakePuppetVertexArray(mdl_mesh.positions.size());

    for (std::size_t i = 0; i < mdl_mesh.positions.size(); ++i) {
        std::array<float, 16> one_vert {};
        uint                  offset = 0;
        memcpy(one_vert.data() + 4 * (offset++),
               mdl_mesh.positions[i].data(),
               sizeof(mdl_mesh.positions[i]));
        if (i < mdl_mesh.blend_indices.size()) {
            memcpy(one_vert.data() + 4 * (offset),
                   mdl_mesh.blend_indices[i].data(),
                   sizeof(mdl_mesh.blend_indices[i]));
        }
        ++offset;
        if (i < mdl_mesh.blend_weights.size()) {
            memcpy(one_vert.data() + 4 * (offset),
                   mdl_mesh.blend_weights[i].data(),
                   sizeof(mdl_mesh.blend_weights[i]));
        }
        ++offset;
        if (i < mdl_mesh.texcoords.size()) {
            memcpy(one_vert.data() + 4 * (offset),
                   mdl_mesh.texcoords[i].data(),
                   sizeof(mdl_mesh.texcoords[i]));
        }
        vertex.SetVertexs(i, one_vert);
    }

    submesh.AddVertexArray(std::move(vertex));
    submesh.AddIndexArray(MakePuppetIndexArray(mdl_mesh.indices));

    std::vector<SceneMesh::DrawRange> ranges;
    ranges.reserve(mdl_mesh.parts.size());
    for (const auto& part : mdl_mesh.parts) {
        ranges.push_back({ .indexOffset = part.start, .indexCount = part.size });
    }
    submesh.SetDrawRanges(ranges);
}
} // namespace

// bytes * size
constexpr uint32_t singile_vertex  = 4 * (3 + 4 + 4 + 2);
constexpr uint32_t singile_indices = 2 * 3;
constexpr uint32_t std_format_vertex_size_herald_value = 0x01800009;

// number of bytes in an MDAT attachment after the attachment name
constexpr uint32_t mdat_attachment_data_byte_length = 64;

// alternative consts for alternative mdl format
constexpr uint32_t alt_singile_vertex = 4 * (3 + 4 + 4 + 2 + 7);
constexpr uint32_t alt_format_vertex_size_herald_value = 0x0180000F;

constexpr uint32_t singile_bone_frame = 4 * 9;

bool WPMdlParser::Parse(std::string_view path, fs::VFS& vfs, WPMdl& mdl) {
    auto str_path = std::string(path);
    auto pfile    = vfs.Open("/assets/" + str_path);
    if (! pfile) return false;
    auto memfile  = fs::MemBinaryStream(*pfile);
    auto& f = memfile;

    mdl.mdlv = ReadMDLVesion(f);
    mdl.mdl_header.mdlv = mdl.mdlv;

    int32_t mdl_flag = f.ReadInt32();
    if (mdl_flag == 9) {
        LOG_INFO("puppet '%s' is not complete, ignore", str_path.c_str());
        return false;
    };
    mdl.mdl_header.mdl_flag = static_cast<uint32_t>(mdl_flag);
    f.ReadInt32(); // unk, 1
    mdl.mdl_header.mesh_count = f.ReadUint32();

    bool alt_mdl_format = false;

    if (mdl.mdlv >= 21) {
        if (mdl.mdl_header.mesh_count == 0 || mdl.mdl_header.mesh_count > kMaxMdlMeshes) {
            LOG_INFO("unsupported mdl mesh count %d in %s",
                     mdl.mdl_header.mesh_count, str_path.c_str());
            return false;
        }
        mdl.meshes.resize(mdl.mdl_header.mesh_count);
        for (auto& mesh : mdl.meshes) {
            if (! ParseMesh(f, mdl.mdl_header, mesh, str_path)) return false;
        }
        MirrorFirstMeshToLegacyFields(mdl);
        if (! PeekBlockMagic(f, "MDLS")) {
            LOG_INFO("read puppet mesh: mdlv: %d, meshes: %zu, no MDLS section",
                     mdl.mdlv,
                     mdl.meshes.size());
            return true;
        }
        mdl.mdls = ReadMDLVesion(f);
    } else {
        mdl.mat_json_file = f.ReadStr();
        // 0
        f.ReadInt32();

        uint32_t curr = f.ReadUint32();

        // if the uint at the normal vertex size position is 0, then this file
        // uses the alternative MDL format, therefore the actual vertex size is
        // located after the herald value, and we'll need to account for other differences later on.
        if(curr == 0){
            alt_mdl_format = true;
            while (curr != alt_format_vertex_size_herald_value){
                const idx before = f.Tell();
                curr = f.ReadUint32();
                if (f.Tell() == before) {
                    LOG_INFO("mdl missing alt vertex herald: %s", str_path.c_str());
                    return false;
                }
            }
            curr = f.ReadUint32();
        }
        else if(curr == std_format_vertex_size_herald_value){
            curr = f.ReadUint32();
        }

        uint32_t vertex_size = curr;
        if (vertex_size % (alt_mdl_format? alt_singile_vertex : singile_vertex) != 0) {
            LOG_INFO("unsupport mdl vertex size %d", vertex_size);
            return false;
        }

        // if using the alternative MDL format, vertexes contain 7 extra 32-bit chunks between
        // position and blend indices
        uint32_t vertex_num = vertex_size / (alt_mdl_format ? alt_singile_vertex : singile_vertex);
        mdl.vertexs.resize(vertex_num);
        for (auto& vert : mdl.vertexs) {
            for (auto& v : vert.position) v = f.ReadFloat();
            if(alt_mdl_format) {for (int i = 0; i < 7; i++) f.ReadUint32();}
            for (auto& v : vert.blend_indices) v = f.ReadUint32();
            for (auto& v : vert.weight) v = f.ReadFloat();
            for (auto& v : vert.texcoord) v = f.ReadFloat();
        }

        uint32_t indices_size = f.ReadUint32();
        if (indices_size % singile_indices != 0) {
            LOG_INFO("unsupport mdl indices size %d", indices_size);
            return false;
        }

        uint32_t indices_num = indices_size / singile_indices;
        mdl.indices.resize(indices_num);
        for (auto& id : mdl.indices) {
            for (auto& v : id) v = f.ReadUint16();
        }

        mdl.mdls = ReadMDLVesion(f);
    }

    size_t bones_file_end = f.ReadUint32();
    (void)bones_file_end;

    uint16_t bones_num = f.ReadUint16();
    // 1 byte
    f.ReadUint16(); // unk

    mdl.puppet  = std::make_shared<WPPuppet>();
    auto& bones = mdl.puppet->bones;
    auto& anims = mdl.puppet->anims;

    bones.resize(bones_num);
    for (uint i = 0; i < bones_num; i++) {
        auto&       bone = bones[i];
        std::string name = f.ReadStr();
        f.ReadInt32(); // unk

        bone.parent = f.ReadUint32();
        if (bone.parent >= i && ! bone.noParent()) {
            LOG_INFO("mdl wrong bone parent index %d", bone.parent);
            return false;
        }
        bone.bind_parent = bone.parent;

        uint32_t size = f.ReadUint32();
        if (size != 64) {
            LOG_INFO("mdl unsupport bones size: %d", size);
            return false;
        }
        for (auto row : bone.transform.matrix().colwise()) {
            for (auto& x : row) x = f.ReadFloat();
        }

        std::string bone_simulation_json = f.ReadStr();
        /*
        auto trans = bone.transform.translation();
        LOG_INFO("trans: %f %f %f", trans[0], trans[1], trans[2]);
        */
    }

    if (mdl.mdlv >= 21) {
        for (auto& bone : bones) {
            if (bone.bind_parent == 0) {
                bone.bind_parent = WPPuppet::Bone::NO_PARENT;
            }
        }
    }

    if (mdl.mdls > 1) {
        int16_t unk = f.ReadInt16();
        if (unk != 0) {
            LOG_INFO("puppet: one unk is not 0, may be wrong");
        }

        uint8_t has_trans = f.ReadUint8();
        if (has_trans) {
            for (uint i = 0; i < bones_num; i++)
                for (uint j = 0; j < 16; j++) f.ReadFloat(); // mat
        }
        uint32_t size_unk = f.ReadUint32();
        for (uint i = 0; i < size_unk; i++)
            for (int j = 0; j < 3; j++) f.ReadUint32();

        f.ReadUint32(); // unk

        uint8_t has_offset_trans = f.ReadUint8();
        if (has_offset_trans) {
            for (uint i = 0; i < bones_num; i++) {
                for (uint j = 0; j < 3; j++) f.ReadFloat();  // like pos
                for (uint j = 0; j < 16; j++) f.ReadFloat(); // mat
            }
        }

        uint8_t has_index = f.ReadUint8();
        if (has_index) {
            for (uint i = 0; i < bones_num; i++) {
                f.ReadUint32();
            }
        }
    }

    // sometimes there can be one or more zero bytes and/or MDAT sections containing
    // attachments before the MDLA section, so we need to skip them
    std::string mdType = "";
    std::string mdVersion;
    
    do {
        const idx before = f.Tell();
        if (before >= f.Size()) {
            LOG_INFO("mdl missing MDLA section: %s", str_path.c_str());
            return false;
        }
        std::string mdPrefix = f.ReadStr();
        if (f.Tell() == before) {
            LOG_INFO("mdl truncated while scanning sections: %s", str_path.c_str());
            return false;
        }

        // sometimes there can be other garbage in this gap, so we need to 
        // skip over that as well
        if(mdPrefix.length() == 8){
            mdType = mdPrefix.substr(0, 4);
            mdVersion = mdPrefix.substr(4, 4);

            if(mdType == "MDAT"){
                f.ReadUint32(); // skip 4 bytes
                uint32_t num_attachments = f.ReadUint16(); // number of attachments in the MDAT section

                for(int i = 0; i < num_attachments; i++){
                    f.ReadUint16(); // skip 2 bytes
                    std::string attachment_name = f.ReadStr(); // attachment name
                    int bytesToRead = mdat_attachment_data_byte_length;
                    for(int j = 0; j < bytesToRead; j++){
                        f.ReadUint8();
                    }

                }
            }
        }
    } while (mdType != "MDLA");
    

    if(mdType == "MDLA" && mdVersion.length() > 0){
        mdl.mdla = std::stoi(mdVersion);
        if (mdl.mdla != 0) {
            uint end_size = f.ReadUint32();
            (void)end_size;

            uint anim_num = f.ReadUint32();
            anims.resize(anim_num);
            for (auto& anim : anims) {
                // there can be a variable number of 32-bit 0s between animations
                anim.id = 0;
                while(anim.id == 0){
                    const idx before = f.Tell();
                    anim.id = f.ReadInt32();
                    if (f.Tell() == before) {
                        LOG_INFO("mdl truncated while reading animation id: %s", str_path.c_str());
                        return false;
                    }
                }
    
                if (anim.id <= 0) {
                    LOG_INFO("wrong anime id %d", anim.id);
                    return false;
                }
                f.ReadInt32();
                anim.name   = f.ReadStr();
                if(anim.name.empty()){
                    anim.name = f.ReadStr();
                }
                anim.mode   = ToPlayMode(f.ReadStr());
                anim.fps    = f.ReadFloat();
                anim.length = f.ReadInt32();
                f.ReadInt32();

                uint32_t b_num = f.ReadUint32();
                anim.bframes_array.resize(b_num);
                for (auto& bframes : anim.bframes_array) {
                    f.ReadInt32();
                    uint32_t byte_size = f.ReadUint32();
                    uint32_t num       = byte_size / singile_bone_frame;
                    if (byte_size % singile_bone_frame != 0) {
                        LOG_INFO("wrong bone frame size %d", byte_size);
                        return false;
                    }
                    bframes.frames.resize(num);
                    for (auto& frame : bframes.frames) {
                        for (auto& v : frame.position) v = f.ReadFloat();
                        for (auto& v : frame.angle) v = f.ReadFloat();
                        for (auto& v : frame.scale) v = f.ReadFloat();
                    }
                }
                
                // in the alternative MDL format there are 2 empty bytes followed
                // by a variable number of 32-bit 0s between animations. We'll read
                // the two bytes now so that the cursor is aligned to read through the
                // 32-bit 0s in the next iteration
                if(alt_mdl_format)
                {
                    f.ReadUint8();
                    f.ReadUint8();    
                }
                else if(mdl.mdla == 3){
                    // In MDLA version 3 there is an extra 8-bit zero between animations.
                    // This will cause the parser to be misaligned moving forward if we don't handle it here.
                    f.ReadUint8();
                }
                else{
                    uint32_t unk_extra_uint = f.ReadUint32();
                    for (uint i = 0; i < unk_extra_uint; i++) {
                        f.ReadFloat();
                        // data is like: {"$$hashKey":"object:2110","frame":1,"name":"random_anim"}
                        std::string unk_extra = f.ReadStr();
                    }
                }
            }
        }
    }
    
    mdl.puppet->prepared();

    LOG_INFO("read puppet: mdlv: %d, nmdls: %d, mdla: %d, bones: %d, anims: %d",
             mdl.mdlv,
             mdl.mdls,
             mdl.mdla,
             mdl.puppet->bones.size(),
             mdl.puppet->anims.size());
    return true;
}

void WPMdlParser::GenPuppetMesh(SceneMesh& mesh, const WPMdl& mdl) {
    if (! mdl.meshes.empty()) {
        auto& submeshes = mesh.Submeshes();
        submeshes.clear();
        submeshes.resize(mdl.meshes.size());
        for (std::size_t i = 0; i < mdl.meshes.size(); ++i) {
            submeshes[i].material_slot = static_cast<uint32_t>(i);
            GenMeshFromMdl(submeshes[i], mdl.meshes[i]);
        }
        return;
    }

    SceneVertexArray vertex = MakePuppetVertexArray(mdl.vertexs.size());

    std::array<float, 16> one_vert;
    auto                  to_one = [](const WPMdl::Vertex& in, decltype(one_vert)& out) {
        uint offset = 0;
        memcpy(out.data() + 4 * (offset++), in.position.data(), sizeof(in.position));
        memcpy(out.data() + 4 * (offset++), in.blend_indices.data(), sizeof(in.blend_indices));
        memcpy(out.data() + 4 * (offset++), in.weight.data(), sizeof(in.weight));
        memcpy(out.data() + 4 * (offset++), in.texcoord.data(), sizeof(in.texcoord));
    };
    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        auto& v = mdl.vertexs[i];
        to_one(v, one_vert);
        vertex.SetVertexs(i, one_vert);
    }
    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(MakePuppetIndexArray(mdl.indices));
}

void WPMdlParser::AddPuppetShaderInfo(WPShaderInfo& info, const WPMdl& mdl) {
    info.combos["SKINNING"]  = "1";
    info.combos["BONECOUNT"] = std::to_string(mdl.puppet->bones.size());
}

void WPMdlParser::AddPuppetMatInfo(wpscene::WPMaterial& mat, const WPMdl& mdl) {
    mat.combos["SKINNING"]  = 1;
    mat.combos["BONECOUNT"] = (i32)mdl.puppet->bones.size();
    mat.use_puppet          = true;
}
