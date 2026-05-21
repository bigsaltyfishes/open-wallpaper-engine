#include "Text/TextLayer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace wallpaper
{

namespace
{
std::size_t LineCount(std::string_view text) {
    if (text.empty()) return 1;
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1u;
}

std::size_t LongestLineCodepoints(std::string_view text) {
    std::size_t longest = 0;
    std::size_t current = 0;
    for (unsigned char ch : text) {
        if (ch == '\n') {
            longest = std::max(longest, current);
            current = 0;
            continue;
        }
        if ((ch & 0xC0u) != 0x80u) ++current;
    }
    return std::max(longest, current);
}
} // namespace

Eigen::Vector2f EstimateTextLayerSize(std::string_view text, float point_size, float padding) {
    if (! std::isfinite(point_size) || point_size <= 0.0f) point_size = 12.0f;
    if (! std::isfinite(padding) || padding < 0.0f) padding = 0.0f;

    const float glyph_width = std::max(1.0f, point_size * 0.6f);
    const float line_height = std::max(1.0f, point_size * 1.2f);
    const auto  columns     = std::max<std::size_t>(1u, LongestLineCodepoints(text));
    const auto  lines       = std::max<std::size_t>(1u, LineCount(text));

    return Eigen::Vector2f(static_cast<float>(columns) * glyph_width + padding * 2.0f,
                           static_cast<float>(lines) * line_height + padding * 2.0f);
}

TextLayer::TextLayer(TextLayerState state): m_state(std::move(state)) {
    if (m_state.resolved_font_identity.empty()) m_state.resolved_font_identity = m_state.font_key;
    if (m_state.resolved_font_kind.empty()) m_state.resolved_font_kind = "family";
    EnsureCacheIdentity();
    Relayout();
    MarkCacheDirty();
    m_state.dirty = false;
}

void TextLayer::SetText(std::string text) {
    if (m_state.text == text) return;
    m_state.text  = std::move(text);
    m_state.dirty = true;
    Relayout();
    MarkCacheDirty();
}

void TextLayer::ClearDirty() {
    m_state.dirty       = false;
    m_state.cache_dirty = false;
    m_state.full_dirty  = false;
}

void TextLayer::Relayout() {
    const auto estimated = EstimateTextLayerSize(m_state.text, m_state.point_size, m_state.padding);
    m_state.layout_size = m_state.explicit_size.x() > 0.0f && m_state.explicit_size.y() > 0.0f
                              ? m_state.explicit_size
                              : estimated;
}

void TextLayer::EnsureCacheIdentity() {
    if (! m_state.texture_cache_key.empty()) return;
    const std::string material = m_state.layer_key + "|" + m_state.text + "|" +
                                 m_state.resolved_font_kind + "|" + m_state.resolved_font_identity +
                                 "|" + m_state.resolved_font_path + "|" +
                                 std::to_string(m_state.point_size);
    m_state.texture_cache_key = "textcache:" + std::to_string(std::hash<std::string> {}(material));
}

void TextLayer::MarkCacheDirty() {
    ++m_state.cache_revision;
    m_state.cache_dirty = true;
    m_state.full_dirty  = true;
}

} // namespace wallpaper
