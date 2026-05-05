#pragma once

#include "WPShaderParser.hpp"

namespace wallpaper::shader
{

struct IncludeExpansionResult
{
    std::string source_without_includes;
    std::string expanded_includes;
};

IncludeExpansionResult ExpandIncludes(fs::VFS& vfs, const std::string& source);
void ExtractMetadata(
    std::string_view source,
    WPShaderInfo* shader_info,
    std::span<const WPShaderTexInfo> tex_infos);
std::string MergeExpandedIncludes(IncludeExpansionResult expansion);

std::string BuildShaderHeader(std::string_view source, const Combos& combos, ShaderType type);
std::string PreprocessStageSource(
    std::string_view source,
    ShaderType type,
    const Combos& combos,
    WPPreprocessorInfo& process_info);
std::string FinalizeStageSource(
    const WPShaderUnit& unit,
    const WPPreprocessorInfo* previous,
    const WPPreprocessorInfo* next);
std::string ApplyCompatibilityRewrites(std::string source, ShaderType type);

} // namespace wallpaper::shader
