#pragma once

#include <Eigen/Dense>

#include <cstdint>
#include <string>
#include <string_view>

namespace wallpaper
{

struct TextLayerState {
    std::string     text;
    std::string     layer_key;
    std::string     font_key;
    std::string     resolved_font_kind { "family" };
    std::string     resolved_font_identity;
    std::string     resolved_font_path;
    float           point_size { 12.0f };
    float           padding { 0.0f };
    Eigen::Vector2f explicit_size { Eigen::Vector2f::Zero() };
    std::string     horizontal_align;
    std::string     vertical_align;
    std::string     anchor;
    bool            dirty { false };
    bool            cache_dirty { false };
    bool            full_dirty { false };
    uint64_t        cache_revision { 0 };
    std::string     texture_cache_key;
    std::string     render_backend { "cpu-layout-only-gpu-rendering-deferred" };
    Eigen::Vector2f layout_size { Eigen::Vector2f::Zero() };
};

class TextLayer {
public:
    explicit TextLayer(TextLayerState state);

    const TextLayerState& state() const { return m_state; }
    const std::string&    text() const { return m_state.text; }
    Eigen::Vector2f       size() const { return m_state.layout_size; }
    bool                  dirty() const { return m_state.dirty; }

    void SetText(std::string text);
    void ClearDirty();

private:
    void Relayout();
    void EnsureCacheIdentity();
    void MarkCacheDirty();

    TextLayerState m_state;
};

Eigen::Vector2f EstimateTextLayerSize(std::string_view text, float point_size, float padding);

} // namespace wallpaper
