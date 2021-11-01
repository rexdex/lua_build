#include "common.h"
#include "toolScriptMake.h"
#include "projectGenerator.h"
#include "solutionGeneratorVS.h"

//--

ToolScriptMake::ToolScriptMake()
{}

int ToolScriptMake::run(const char* argv0, const Commandline& cmdline)
{
    auto projectPath = cmdline.get("projectDir");
    if (projectPath.empty())
    {
        std::cerr << "Required parameter -projectDir not specified\n";
        return -1;
    }

    auto projectName = cmdline.get("projectName");
    if (projectName.empty())
    {
        std::cerr << "Required parameter -projectName not specified\n";
        return -1;
    }

    if (!fs::is_directory(projectPath))
    {
        std::cerr << "Specified directory '" << projectPath << "' is not a valid directory\n";
        return -1;
    }

    auto languageExtension = "";
    auto languageType = cmdline.get("language");
    if (languageType == "mono")
    {
        languageExtension = "cs";
    }
    else
    {
        std::cerr << "Specified language '" << languageType << "' is not supported\n";
        return -1;
    }

    auto generator = cmdline.get("generator");
    if (generator.empty())
        generator = "vs";

    //---

    auto rootPath = fs::path(projectPath) / "scripts" / languageType;
    rootPath.make_preferred();
    std::cout << "Scanning script sources at '" << rootPath << "'\n";

    ScriptProjectStructure structure(rootPath, projectName);
    structure.scan();

    if (structure.files.empty())
    {
        std::cerr << "No script files found at '" << rootPath << "'\n";
        return -1;
    }

    std::cout << "Found " << structure.files.size() << " script files!\n";

    //--

    FileGenerator files;

    /*if (generator == "vs")
    {
        ScriptSolutionGeneratorVS gen(structure, files);
        gen.generateProjects();
        gen.generateSolution();
    }*/

    if (!files.saveFiles())
        return false;

    return true;
}

//--
