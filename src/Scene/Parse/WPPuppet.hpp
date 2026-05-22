#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <span>
#include <Eigen/Geometry>

#include "Core/Literals.hpp"

namespace wallpaper
{

class WPPuppetLayer;

class WPPuppet {
public:
    enum class PlayMode
    {
        Loop,
        Mirror,
        Single
    };
    struct Bone {
        static constexpr uint32_t NO_PARENT { 0xFFFFFFFFu };

        Eigen::Affine3f transform { Eigen::Affine3f::Identity() };
        uint32_t        parent { NO_PARENT };
        // Runtime animation follows parent. MDLV21 bind transforms flatten only
        // root-anchored children via bind_parent to avoid double translation.
        uint32_t        bind_parent { NO_PARENT };

        bool noParent() const { return parent == NO_PARENT; }
        // prepared
        Eigen::Affine3f offset_trans { Eigen::Affine3f::Identity() };
        /*
        Eigen::Vector3f world_axis_x;
        Eigen::Vector3f world_axis_y;
        Eigen::Vector3f world_axis_z;
        */
    };
    struct BoneFrame {
        Eigen::Vector3f position;
        Eigen::Vector3f angle;
        Eigen::Vector3f scale;

        // prepared
        Eigen::Quaterniond quaternion;
    };
    struct Animation {
        i32         id;
        double      fps;
        i32         length;
        PlayMode    mode;
        std::string name;

        struct BoneFrames {
            std::vector<BoneFrame> frames;
        };
        std::vector<BoneFrames> bframes_array;

        // prepared
        double max_time;
        double frame_time;
        struct InterpolationInfo {
            idx    frame_a;
            idx    frame_b;
            double t;
        };
        InterpolationInfo getInterpolationInfo(double* cur_time) const;
    };

public:
    std::vector<Bone>      bones;
    std::vector<Animation> anims;

    std::span<const Eigen::Affine3f> genFrame(WPPuppetLayer&, double time) noexcept;
    void                             prepared();

private:
    std::vector<Eigen::Affine3f> m_final_affines;
};

class WPPuppetLayer {
    friend class WPPuppet;

public:
    WPPuppetLayer();
    WPPuppetLayer(std::shared_ptr<WPPuppet>);
    ~WPPuppetLayer();

    bool hasPuppet() const { return (bool)m_puppet; };

    struct AnimationLayer {
        i32         id { 0 };
        double      rate { 1.0f };
        double      blend { 1.0f };
        bool        visible { true };
        double      cur_time { 0.0f };
        i32         layer_id { 0 };
        std::string name;
        bool        additive { false };
        bool        blendin { false };
        bool        blendout { false };
        double      blendtime { 0.0 };
    };

    void prepared(std::span<AnimationLayer>);

    std::span<const Eigen::Affine3f> genFrame(double time) noexcept;

    void updateInterpolation(double time) noexcept;

private:
    struct Layer {
        AnimationLayer                         anim_layer;
        double                                 blend;
        const WPPuppet::Animation*             anim { nullptr };
        WPPuppet::Animation::InterpolationInfo interp_info {};

        operator bool() const noexcept { return anim != nullptr; };
    };

    double m_global_blend { 1.0 };
    double m_total_blend { 0.0 };

    std::vector<Layer>        m_layers;
    std::shared_ptr<WPPuppet> m_puppet;
};

} // namespace wallpaper
