#pragma once
#include <regex>
#include <string>
namespace RTE {
inline std::string BrowserShaderSource(std::string dataString) {
    const auto version = dataString.find("#version");
    if (version != std::string::npos) dataString.erase(0, version);
	// WebGL 2 preserves the engine's shader logic with GLSL ES 3.00 syntax.
	dataString = std::regex_replace(dataString, std::regex("#version 330 core"),
	    "#version 300 es\nprecision highp float;\nprecision highp int;");
	dataString = std::regex_replace(dataString, std::regex("#extension[^\n]*"), "");
	dataString = std::regex_replace(dataString, std::regex("(uniform[^;=]+)=[^;]+;"), "$1;");
	dataString = std::regex_replace(dataString, std::regex("([0-9]\\.[0-9]+)[fF]"), "$1");
return dataString;
}
}
