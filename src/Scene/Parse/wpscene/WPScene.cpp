#include "WPScene.h"

using namespace wallpaper::wpscene;

bool Orthogonalprojection::FromJson(const nlohmann::json& json) {
    if(json.is_null()) return false;
	if(json.contains("auto")) {
		GET_JSON_NAME_VALUE(json, "auto", auto_);
	}
	else {
		GET_JSON_NAME_VALUE(json, "width", width);
		GET_JSON_NAME_VALUE(json, "height", height);
	}
    return true;
}

bool WPSceneCamera::FromJson(const nlohmann::json& json) {
    GET_JSON_NAME_VALUE(json, "center", center);
    GET_JSON_NAME_VALUE(json, "eye", eye);
    GET_JSON_NAME_VALUE(json, "up", up);
    return true;
}

bool WPSceneGeneral::FromJson(const nlohmann::json& json) {
    return FromJson(json, kSceneVersionUnknown);
}

namespace
{
bool WantsVersion(uint16_t pkg_version, uint16_t gate) {
    return pkg_version == WPSceneGeneral::kSceneVersionUnknown || pkg_version >= gate;
}
}

bool WPSceneGeneral::FromJson(const nlohmann::json& json, uint16_t pkg_version) {
    GET_JSON_NAME_VALUE(json, "ambientcolor", ambientcolor);
    GET_JSON_NAME_VALUE(json, "skylightcolor", skylightcolor);
	GET_JSON_NAME_VALUE(json, "clearcolor", clearcolor);
	GET_JSON_NAME_VALUE_NOWARN(json, "clearenabled", clearenabled);
	GET_JSON_NAME_VALUE(json, "cameraparallax", cameraparallax);
	GET_JSON_NAME_VALUE(json, "cameraparallaxamount", cameraparallaxamount);
	GET_JSON_NAME_VALUE(json, "cameraparallaxdelay", cameraparallaxdelay);
	GET_JSON_NAME_VALUE(json, "cameraparallaxmouseinfluence", cameraparallaxmouseinfluence);
	GET_JSON_NAME_VALUE_NOWARN(json, "zoom", zoom);
	GET_JSON_NAME_VALUE_NOWARN(json, "fov", fov);
	GET_JSON_NAME_VALUE_NOWARN(json, "nearz", nearz);
	GET_JSON_NAME_VALUE_NOWARN(json, "farz", farz);
	GET_JSON_NAME_VALUE_NOWARN(json, "bloom", bloom);
	GET_JSON_NAME_VALUE_NOWARN(json, "bloomstrength", bloomstrength);
	GET_JSON_NAME_VALUE_NOWARN(json, "bloomthreshold", bloomthreshold);
    if (WantsVersion(pkg_version, 10)) {
	    GET_JSON_NAME_VALUE_NOWARN(json, "hdr", hdr);
	    GET_JSON_NAME_VALUE_NOWARN(json, "norecompile", norecompile);
    }
    if (WantsVersion(pkg_version, 20)) {
	    GET_JSON_NAME_VALUE_NOWARN(json, "bloomtint", bloomtint);
    }
    if (WantsVersion(pkg_version, 21)) {
	    GET_JSON_NAME_VALUE_NOWARN(json, "perspectiveoverridefov", perspectiveoverridefov);
	    GET_JSON_NAME_VALUE_NOWARN(json, "windenabled", windenabled);
	    GET_JSON_NAME_VALUE_NOWARN(json, "winddirection", winddirection);
	    GET_JSON_NAME_VALUE_NOWARN(json, "windstrength", windstrength);
	    GET_JSON_NAME_VALUE_NOWARN(json, "gravitydirection", gravitydirection);
	    GET_JSON_NAME_VALUE_NOWARN(json, "gravitystrength", gravitystrength);
    }
    if (WantsVersion(pkg_version, 22)) {
	    GET_JSON_NAME_VALUE_NOWARN(json, "transparentsorting", transparentsorting);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogdistance", fogdistance);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogdistancestart", fogdistancestart);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogdistanceend", fogdistanceend);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogdistancecolor", fogdistancecolor);
    }
    if (WantsVersion(pkg_version, 23)) {
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogheight", fogheight);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogheightstart", fogheightstart);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogheightend", fogheightend);
	    GET_JSON_NAME_VALUE_NOWARN(json, "fogheightcolor", fogheightcolor);
    }
    if (WantsVersion(pkg_version, 21) &&
        json.contains("lightconfig") && json.at("lightconfig").is_object()) {
        lightconfig = json.at("lightconfig");
    }
    if(json.contains("orthogonalprojection")) {
        const auto& ortho = json.at("orthogonalprojection");
        if(ortho.is_null())
            isOrtho = false;
        else {
            isOrtho = true;
            orthogonalprojection.FromJson(ortho);
        }
    }
    return true;
}

bool WPScene::FromJson(const nlohmann::json& json) {
    return FromJson(json, WPSceneGeneral::kSceneVersionUnknown);
}

bool WPScene::FromJson(const nlohmann::json& json, uint16_t pkg_version) {
    this->pkg_version = pkg_version;
    if(json.contains("camera")) {
        camera.FromJson(json.at("camera"));
    } else {
        LOG_ERROR("scene no camera");
        return false;
    }
    if(json.contains("general")) {
        general.FromJson(json.at("general"), pkg_version);
    } else {
        LOG_ERROR("scene no genera data");
        return false;
    }
    return true; 
}
