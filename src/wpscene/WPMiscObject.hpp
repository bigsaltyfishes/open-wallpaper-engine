#pragma once

#include "WPJson.hpp"
#include "WPObjectSchema.hpp"

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace wallpaper
{
namespace fs
{
class VFS;
}

namespace wpscene
{

struct WPMiscObjectBase {
    int32_t              id { 0 };
    int32_t              parent_id { -1 };
    std::string          name;
    std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> angles { 0.0f, 0.0f, 0.0f };
    std::array<float, 2> parallaxDepth { 0.0f, 0.0f };
    bool                 visible { true };
    bool                 locktransforms { false };
    bool                 muteineditor { false };
    bool                 nointerpolation { false };
    std::vector<int32_t> dependencies;
    nlohmann::json       instance;
    nlohmann::json       field_bindings;

    void FromCommonJson(const nlohmann::json& json) {
        GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
        GET_JSON_NAME_VALUE_NOWARN(json, "name", name);
        GET_JSON_NAME_VALUE_NOWARN(json, "origin", origin);
        GET_JSON_NAME_VALUE_NOWARN(json, "scale", scale);
        GET_JSON_NAME_VALUE_NOWARN(json, "angles", angles);
        GET_JSON_NAME_VALUE_NOWARN(json, "parallaxDepth", parallaxDepth);
        GET_JSON_NAME_VALUE_NOWARN(json, "visible", visible);
        GET_JSON_NAME_VALUE_NOWARN(json, "locktransforms", locktransforms);
        GET_JSON_NAME_VALUE_NOWARN(json, "muteineditor", muteineditor);
        GET_JSON_NAME_VALUE_NOWARN(json, "nointerpolation", nointerpolation);
        GET_JSON_NAME_VALUE_NOWARN(json, "parent", parent_id);
        ParseDependencies(json, dependencies);
        if (json.contains("instance")) instance = json.at("instance");
        AbsorbFieldBindings(json, field_bindings);
    }
};

struct WPTextObject : WPMiscObjectBase {
    nlohmann::json       text;
    nlohmann::json       font;
    float                pointsize { 12.0f };
    uint32_t             padding { 0 };
    std::string          horizontalalign;
    std::string          verticalalign;
    std::string          anchor;
    std::string          alignment { "center" };
    uint32_t             maxrows { 0 };
    float                maxwidth { 0.0f };
    bool                 limitrows { false };
    bool                 limitwidth { false };
    bool                 limituseellipsis { false };
    std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
    float                alpha { 1.0f };
    float                brightness { 1.0f };
    int32_t              colorBlendMode { 0 };
    std::array<float, 2> size { 0.0f, 0.0f };
    bool                 perspective { false };
    bool                 copybackground { false };
    bool                 solid { false };
    bool                 opaquebackground { false };
    bool                 ledsource { false };
    std::array<float, 3> backgroundcolor { 0.0f, 0.0f, 0.0f };
    float                backgroundbrightness { 1.0f };

    bool FromJson(const nlohmann::json& json, fs::VFS&) {
        FromCommonJson(json);
        if (json.contains("text")) text = json.at("text");
        if (json.contains("font")) font = json.at("font");
        GET_JSON_NAME_VALUE_NOWARN(json, "pointsize", pointsize);
        GET_JSON_NAME_VALUE_NOWARN(json, "padding", padding);
        GET_JSON_NAME_VALUE_NOWARN(json, "horizontalalign", horizontalalign);
        GET_JSON_NAME_VALUE_NOWARN(json, "verticalalign", verticalalign);
        GET_JSON_NAME_VALUE_NOWARN(json, "anchor", anchor);
        GET_JSON_NAME_VALUE_NOWARN(json, "alignment", alignment);
        GET_JSON_NAME_VALUE_NOWARN(json, "maxrows", maxrows);
        GET_JSON_NAME_VALUE_NOWARN(json, "maxwidth", maxwidth);
        GET_JSON_NAME_VALUE_NOWARN(json, "limitrows", limitrows);
        GET_JSON_NAME_VALUE_NOWARN(json, "limitwidth", limitwidth);
        GET_JSON_NAME_VALUE_NOWARN(json, "limituseellipsis", limituseellipsis);
        GET_JSON_NAME_VALUE_NOWARN(json, "color", color);
        GET_JSON_NAME_VALUE_NOWARN(json, "alpha", alpha);
        GET_JSON_NAME_VALUE_NOWARN(json, "brightness", brightness);
        GET_JSON_NAME_VALUE_NOWARN(json, "colorBlendMode", colorBlendMode);
        GET_JSON_NAME_VALUE_NOWARN(json, "size", size);
        GET_JSON_NAME_VALUE_NOWARN(json, "perspective", perspective);
        GET_JSON_NAME_VALUE_NOWARN(json, "copybackground", copybackground);
        GET_JSON_NAME_VALUE_NOWARN(json, "solid", solid);
        GET_JSON_NAME_VALUE_NOWARN(json, "opaquebackground", opaquebackground);
        GET_JSON_NAME_VALUE_NOWARN(json, "ledsource", ledsource);
        GET_JSON_NAME_VALUE_NOWARN(json, "backgroundcolor", backgroundcolor);
        GET_JSON_NAME_VALUE_NOWARN(json, "backgroundbrightness", backgroundbrightness);
        return true;
    }
};

struct WPModelObject : WPMiscObjectBase {
    std::string model;
    std::string attachment;
    bool        perspective { false };

    bool FromJson(const nlohmann::json& json, fs::VFS&) {
        FromCommonJson(json);
        GET_JSON_NAME_VALUE_NOWARN(json, "model", model);
        GET_JSON_NAME_VALUE_NOWARN(json, "attachment", attachment);
        GET_JSON_NAME_VALUE_NOWARN(json, "perspective", perspective);
        return true;
    }
};

struct WPCameraObject : WPMiscObjectBase {
    std::string camera;
    std::string path;
    std::string queuemode;
    float       fov { 50.0f };
    float       zoom { 1.0f };
    bool        solid { false };
    bool        disablepropagation { false };

    bool FromJson(const nlohmann::json& json, fs::VFS&) {
        FromCommonJson(json);
        GET_JSON_NAME_VALUE_NOWARN(json, "camera", camera);
        GET_JSON_NAME_VALUE_NOWARN(json, "path", path);
        GET_JSON_NAME_VALUE_NOWARN(json, "queuemode", queuemode);
        GET_JSON_NAME_VALUE_NOWARN(json, "fov", fov);
        GET_JSON_NAME_VALUE_NOWARN(json, "zoom", zoom);
        GET_JSON_NAME_VALUE_NOWARN(json, "solid", solid);
        GET_JSON_NAME_VALUE_NOWARN(json, "disablepropagation", disablepropagation);
        return true;
    }
};

} // namespace wpscene
} // namespace wallpaper
