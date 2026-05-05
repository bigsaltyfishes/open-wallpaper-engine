#pragma once

#include "WPShaderParser.hpp"

namespace wallpaper::shader
{

struct StructuredStageSource
{
    std::string source;
    WPPreprocessorInfo preprocess_info;
};

StructuredStageSource LegalizeStageSource(
    std::string_view preprocessed_source,
    ShaderType type);

} // namespace wallpaper::shader
