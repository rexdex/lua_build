#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <algorithm>
#include <filesystem>

#include <assert.h>

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#include "lua/lstate.h"
}

enum class PlatformType : uint8_t {
    Windows,
    UWP,
    Linux,
    Scarlett,
    Prospero,

    MAX,
};

enum class ConfigurationType : uint8_t {
    Debug,
    Checked,
    Release,
    Final, // release + even more code removed

    MAX,
};

enum class BuildType : uint8_t {
    Development, // includes development projects - editor, asset import, etc
    Standalone, // glued standalone module files
    Shipment, // only final shipable executable (just game)

    MAX,
};

enum class LibraryType : uint8_t {
    Shared, // dlls
    Static, // static libs

    MAX,
};

enum class GeneratorType : uint8_t {
    VisualStudio19,
    VisualStudio22,
    CMake,

    MAX,
};

enum class ProjectFilePlatformFilter : uint8_t
{
    Any,

    Generic_POSIX, // platforms with POSIX functions
    Generic_Console, // generic game console platform
    Generic_DirectX, // generic platform with DirectX (Windows + Xbox)
    Generic_OpenGL, // generic platform with OpenGL (Windows + Linux)

    Windows, // explicit windows platform
    UWP, // explicit UWP platform
    Linux, // explicit Linux platform
    Scarlett,
    Prospero,

    Invalid,
};

namespace fs = std::filesystem;


