#pragma once

#include "utils.h"
#include "project.h"

//--

class ToolScriptMake
{
public:
    ToolScriptMake();

    int run(const char* argv0, const Commandline& cmdline);
};


//--