#ifndef _MSC_VER
#include <stdio.h>
#include <termios.h>
#endif

#include "common.h"
#include "toolMake.h"
#include "toolReflection.h"
#include "projectGenerator.h"
#include "solutionGeneratorVS.h"
#include "solutionGeneratorCMAKE.h"

//--

#ifdef _MSC_VER
#include <Windows.h>
#include <conio.h>

static void ClearConsole()
{
    COORD topLeft = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(console, topLeft);
}
#else
static void ClearConsole()
{
    std::cout << "\x1B[2J\x1B[H";
}

static struct termios old, current;

void initTermios() {
    tcgetattr(0, &old);
    current = old;
    current.c_lflag &= ~ICANON;
    current.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &current);
}

void resetTermios() {
    tcsetattr(0, TCSANOW, &old);
}

char _getch()
{
    char ch;
    initTermios();
    ch = getchar();
    resetTermios();
    return ch;
}

#endif

template< typename T >
bool ConfigEnum(T& option, const char* title)
{
    ClearConsole();

    int maxOption = (int)T::MAX;

    std::cout << title << std::endl;
    std::cout << std::endl;
    std::cout << "Options:\n";

    for (int i = 0; i < maxOption; ++i)
    {
        std::cout << "  " << (i + 1) << "): ";
        std::cout << NameEnumOption((T)i);

        if (option == (T)i)
            std::cout << " (current)";
        std::cout << std::endl;
    }
    std::cout << std::endl;
    std::cout << std::endl;

    std::cout << "Press (1-" << maxOption << ") to select option\n";
    std::cout << "Press (ENTER) to use current option (" << NameEnumOption(option) << ")\n";
    std::cout << "Press (ESC) to exit\n";

    for (;;)
    {
        auto ch = _getch();
        std::cout << "Code: " << (int)ch << std::endl;
        if (ch == 13 || ch == 10)
            return true;
        if (ch == 27)
            return false;

        if (ch >= '1' && ch < ('1' + maxOption))
        {
            option = (T)(ch - '1');
            return true;
        }
    }
}

static void PrintConfig(const Configuration& cfg)
{
    std::cout << "  Platform  : " << NameEnumOption(cfg.platform) << std::endl;
    std::cout << "  Generator : " << NameEnumOption(cfg.generator) << std::endl;
    std::cout << "  Build     : " << NameEnumOption(cfg.build) << std::endl;
    std::cout << "  Libraries : " << NameEnumOption(cfg.libs) << std::endl;
    std::cout << "  Config    : " << NameEnumOption(cfg.configuration) << std::endl;
}

bool RunInteractiveConfig(Configuration& cfg)
{
    const auto configPath = cfg.builderEnvPath / ".buildConfig";

    if (cfg.load(configPath))
    {
        std::cout << std::endl;
        std::cout << "Loaded existing configuration:" << std::endl;
        PrintConfig(cfg);
        std::cout << std::endl;
        std::cout << std::endl;
        std::cout << "Press (ENTER) to use" << std::endl;
        std::cout << "Press (ESC) to edit" << std::endl;

        for (;;)
        {
            auto ch = _getch();
            std::cout << "Code: " << (int)ch << std::endl;
            if (ch == 13 || ch == 10)
                return true;
            if (ch == 27)
                break;
        }
    }

    ClearConsole();
    if (!ConfigEnum(cfg.platform, "Select build platform:"))
        return false;

    if (cfg.platform == PlatformType::Windows || cfg.platform == PlatformType::UWP)
    {
        ClearConsole();
        if (!ConfigEnum(cfg.generator, "Select solution generator:"))
            return false;
    }
    else if (cfg.platform == PlatformType::Prospero || cfg.platform == PlatformType::Scarlett)
    {
        cfg.generator = GeneratorType::VisualStudio19;
    }
    else
    {
        cfg.generator = GeneratorType::CMake;
    }

    ClearConsole();
    if (!ConfigEnum(cfg.build, "Select build type:"))
        return false;

    ClearConsole();
    if (!ConfigEnum(cfg.libs, "Select libraries type:"))
        return false;

    ClearConsole();
    if (!ConfigEnum(cfg.configuration, "Select configuration type:"))
        return false;

    ClearConsole();

    if (!cfg.save(configPath))
        std::cout << "Failed to save configuration!\n";

    std::cout << "Running with config:\n";
    PrintConfig(cfg);

    return true;
}

//--

ToolMake::ToolMake()
{}

int ToolMake::run(const char* argv0, const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!config.parseOptions(argv0, cmdline)) {
        std::cout << "Invalid/incomplete configuration\n";
        return -1;
    }

    if (cmdline.has("interactive"))
        if (!RunInteractiveConfig(config))
            return false;

    if (!config.parsePaths(argv0, cmdline)) {
        std::cout << "Invalid/incomplete configuration\n";
        return -1;
    }

    //--

    ProjectStructure structure;

    if (!config.engineSourcesPath.empty())
        structure.scanProjects(ProjectGroupType::Engine, config.engineSourcesPath);

    /*if (!config.engineScriptPath.empty())
        structure.scanScriptProjects(ProjectGroupType::MonoScripts, config.engineScriptPath / "src");*/

    if (!config.projectSourcesPath.empty())
        structure.scanProjects(ProjectGroupType::User, config.projectSourcesPath);

    uint32_t totalFiles = 0;
    if (!structure.scanContent(totalFiles))
        return -1;

    if (!structure.setupProjects(config))
        return -1;

    if (!structure.resolveProjectDependencies(config))
        return -1;

    if (config.build == BuildType::Standalone)
        if (!structure.makeModules(config))
            return -1;

    std::cout << "Found " << totalFiles << " total files across " << structure.projects.size() << " projects\n";

    if (!structure.deployFiles(config))
        return -1;

    ProjectGenerator codeGenerator(config);
    if (!codeGenerator.extractProjects(structure))
        return -1;

    if (!codeGenerator.generateAutomaticCode())
        return -1;

    if (!codeGenerator.generateExtraCode())
        return -1;

    if (config.generator == GeneratorType::VisualStudio19 || config.generator == GeneratorType::VisualStudio22)
    {
        SolutionGeneratorVS gen(config, codeGenerator);
        if (!gen.generateSolution())
            return -1;
        if (!gen.generateProjects())
            return -1;
    }
    else if (config.generator == GeneratorType::CMake)
    {
        ToolReflection tool;

        /*Commandline fakeCommandLine;
        fakeCommandLine.args.emplace_back({"list", )
        if (!tool.run("", 
            return -1;*/

        SolutionGeneratorCMAKE gen(config, codeGenerator);
        if (!gen.generateSolution())
            return -1;
        if (!gen.generateProjects())
            return -1;
    }

    if (!codeGenerator.saveFiles())
        return -1;

    return 0;
}

//--
