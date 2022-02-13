#include "common.h"
#include "project.h"
#include "projectGenerator.h"

//--

FileGenerator::GeneratedFile* FileGenerator::createFile(const fs::path& path)
{
    auto file = new GeneratedFile(path);
    files.push_back(file);
    return file;
}

bool FileGenerator::saveFiles()
{
    bool valid = true;

    uint32_t numSavedFiles = 0;
    for (const auto* file : files)
        valid &= SaveFileFromString(file->absolutePath, file->content.str(), false, &numSavedFiles, file->customtTime);

    std::cout << "Saved " << numSavedFiles << " files\n";

    if (!valid)
    {
        std::cout << "Failed to save some output files, generated solution may not be valid\n";
        return false;
    }

    return true;
}

//--

ProjectGenerator::ProjectGenerator(const Configuration& config)
    : config(config)
{
    rootGroup = new GeneratedGroup;
    rootGroup->name = "InfernoEngine";
    rootGroup->mergedName = "InfernoEngine";
    rootGroup->assignedVSGuid = GuidFromText(rootGroup->mergedName);

    sharedGlueFolder = config.solutionPath / "generated" / "_shared";
}

struct OrderedGraphBuilder
{
    std::unordered_map<ProjectGenerator::GeneratedProject*, int> depthMap;    

    bool insertProject(ProjectGenerator::GeneratedProject* p, int depth, std::vector<ProjectGenerator::GeneratedProject*>& stack)
    {
        if (find(stack.begin(), stack.end(), p) != stack.end())
        {
            std::cout << "Recursive project dependencies found when project '" << p->mergedName << "' was encountered second time\n";
            for (const auto* proj : stack)
                std::cout << "  Reachable from '" << proj->mergedName << "'\n";
            return false;
        }

        auto currentDepth = depthMap[p];
        if (depth > currentDepth)
        {
            depthMap[p] = depth;

            stack.push_back(p);

            for (auto* dep : p->directDependencies)
                if (!insertProject(dep, depth + 1, stack))
                    return false;

            stack.pop_back();
        }

        return true;
    }

    void extractOrderedList(std::vector<ProjectGenerator::GeneratedProject*>& outList) const
    {
        std::vector<std::pair<ProjectGenerator::GeneratedProject*, int>> pairs;
        std::copy(depthMap.begin(), depthMap.end(), std::back_inserter(pairs));
        std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) -> bool {
            if (a.second != b.second)
                return a.second > b.second;
            return a.first->mergedName < b.first->mergedName;
            });
        
        for (const auto& pair : pairs)
            outList.push_back(pair.first);
    }
};

ProjectGenerator::GeneratedGroup* ProjectGenerator::findOrCreateGroup(std::string_view name, GeneratedGroup* parent)
{
    for (auto* group : parent->children)
        if (group->name == name)
            return group;

    auto* group = new GeneratedGroup;
    group->name = name;
    group->mergedName = parent->mergedName + "." + std::string(name);
    group->parent = parent;
    group->assignedVSGuid = GuidFromText(group->mergedName);
    parent->children.push_back(group);
    return group;
}

ProjectGenerator::GeneratedGroup* ProjectGenerator::createGroup(std::string_view name)
{
    std::vector<std::string_view> parts;
    SplitString(name, ".", parts);

    auto* cur = rootGroup;
    for (const auto& part : parts)
        cur = findOrCreateGroup(part, cur);

    return cur;
}

bool ProjectGenerator::extractProjects(const ProjectStructure& structure)
{
    // validate some settings
    if (config.platform == PlatformType::UWP)
    {
        if (config.libs != LibraryType::Static)
        {
            std::cout << "UWP platform requires 'static' library configuration\n";
            return false;
        }

        if (config.build != BuildType::Shipment)
        {
            std::cout << "UWP platform requires 'shipment' build type\n";
            return false;
        }
    }

    // create projects
    for (const auto* proj : structure.projects)
    {
        if (proj->type == ProjectType::LocalApplication || proj->type == ProjectType::LocalLibrary || proj->type == ProjectType::RttiGenerator || proj->type == ProjectType::EmbeddedMedia)
        {
            // do not include dev-only project in standalone builds
            if (config.build != BuildType::Development)
            {
                if (proj->flagDevOnly)
                    continue;

                if (!CheckPlatformFilter(proj->filter, config.platform))
                {
                    std::cout << "Skipped project '" << proj->mergedName << "' because its not compatible with current platform\n";
                    continue;
                }
            }

            // create wrapper
            auto* generatorProject = new GeneratedProject;
            generatorProject->mergedName = proj->mergedName;
            generatorProject->originalProject = proj;
            generatorProject->generatedPath = config.solutionPath / "generated" / proj->mergedName;
            generatorProject->projectPath = config.solutionPath / "projects" / proj->mergedName;
            generatorProject->outputPath = config.solutionPath / "output" / proj->mergedName;
            generatorProject->hasEmbeddedFiles = proj->hasMedia;

            projects.push_back(generatorProject);
            projectsMap[proj] = generatorProject;

            // create file wrappers
            for (const auto* file : proj->files)
            {
                auto* info = new GeneratedProjectFile;
                info->absolutePath = file->absolutePath;
                info->filterPath = fs::relative(file->absolutePath.parent_path(), proj->rootPath).u8string();
                if (info->filterPath == ".")
                    info->filterPath.clear();
                info->name = file->name;
                info->originalFile = file;
                info->type = file->type;
                info->useInCurrentBuild = file->checkFilter(config.platform);

                if (config.build != BuildType::Development)
                {
                    if (EndsWith(file->name, "_test.cpp") || EndsWith(file->name, "_tests.cpp"))
                    {
                        info->useInCurrentBuild = false;
                    }
                }

                generatorProject->files.push_back(info);
            }

            // add project to group
            bool fullProject = (proj->type == ProjectType::LocalApplication || proj->type == ProjectType::LocalLibrary);
            generatorProject->group = createGroup(fullProject ? PartBefore(proj->mergedName, "_") : "");
            generatorProject->group->projects.push_back(generatorProject);

            // determine project guid
            generatorProject->assignedVSGuid = GuidFromText(generatorProject->mergedName);

            // determine if this project will be a DLL
            if (proj->type == ProjectType::LocalLibrary)
            {
                if (proj->flagForceSharedLibrary)
                    generatorProject->willBeDLL = true;
                else if (proj->flagForceStaticLibrary)
                    generatorProject->willBeDLL = false;
                else
                    generatorProject->willBeDLL = (config.libs == LibraryType::Shared);

                if (config.libs == LibraryType::Shared)
                    generatorProject->willBeLinked = generatorProject->willBeDLL;
                else
                    generatorProject->willBeLinked = true;
            }
            else
            {
                generatorProject->willBeDLL = false;

                if (proj->type == ProjectType::LocalApplication)
                {
                    generatorProject->willBeLinked = true;
                    generatorProject->willHaveEntryPoint = true;
                }
            }
        }

        // create script projects
        else if (proj->type == ProjectType::MonoScriptProject)
        {
            auto* scriptProject = new ScriptProject;
            scriptProject->name = proj->mergedName;
            scriptProject->projectPath = proj->rootPath;

            if (!proj->assignedVSGuid.empty())
            {
                scriptProject->assignedVSGuid = proj->assignedVSGuid;
            }
            else
            {
                scriptProject->assignedVSGuid = GuidFromText(proj->mergedName);
            }

            if (!proj->assignedProjectFile.empty())
            {
                scriptProject->projectFile = proj->rootPath / proj->assignedProjectFile;
            }
            else
            {
                scriptProject->projectFile = proj->rootPath / proj->mergedName;
                scriptProject->projectFile += ".csproj";
            }

            scriptProjects.push_back(scriptProject);
        }
    }

    // map dependencies
    for (auto* proj : projects)
    {
        for (const auto* dep : proj->originalProject->resolvedDependencies)
        {
            auto it = projectsMap.find(dep);
            if (it != projectsMap.end())
            {
                // add a dependency only if it's compatible with platform
                if (CheckPlatformFilter(it->second->originalProject->filter, config.platform))
                    proj->directDependencies.push_back(it->second);
            }
        }
    }

    // build merged dependencies
    bool validDeps = true;
    for (auto* proj : projects)
    {
        std::vector<ProjectGenerator::GeneratedProject*> stack;
        stack.push_back(proj);

        OrderedGraphBuilder graph;
        for (auto* dep : proj->directDependencies)
            validDeps &= graph.insertProject(dep, 1, stack);
        graph.extractOrderedList(proj->allDependencies);
    }

    // build final project list
    {
        auto temp = std::move(projects);

        std::vector<ProjectGenerator::GeneratedProject*> stack;

        OrderedGraphBuilder graph;
        for (auto* proj : temp)
            validDeps &= graph.insertProject(proj, 1, stack);

        projects.clear();
        graph.extractOrderedList(projects);
    }

    // determine what should be generated
    for (auto* proj : projects)
    {
        if (!proj->originalProject->flagModuleRoot)
        {
            const auto publicHeader = proj->originalProject->rootPath / "include/public.h";
            if (fs::is_regular_file(publicHeader))
                proj->localPublicHeader = publicHeader;
        }
    }

    // extract base include directories (source code roots)
    for (const auto* group : structure.groups)
        sourceRoots.push_back(group->rootPath);

    return validDeps;
}

//--

bool ProjectGenerator::generateAutomaticCode()
{
    bool valid = true;

	for (auto* proj : projects)
        if (proj->mergedName != "_rtti_gen")
		    valid &= generateAutomaticCodeForProject(proj);

    for (auto* proj : projects)
        if (proj->mergedName == "_rtti_gen")
            valid &= generateAutomaticCodeForProject(proj);

    return valid;
}

bool ProjectGenerator::generateExtraCode()
{
    bool valid = true;

    for (auto* proj : projects)
        valid &= generateExtraCodeForProject(proj);

    return valid;    
}

bool ProjectGenerator::generateAutomaticCodeForProject(GeneratedProject* project)
{
    bool valid = true;

    if (project->originalProject->type == ProjectType::LocalApplication || project->originalProject->type == ProjectType::LocalLibrary)
    {
        bool needsReflection = false;

        if (project->originalProject->flagModuleRoot)
        {
            needsReflection = true;
        }
        else if (project->mergedName == "core_object")
        {
            needsReflection = true;
        }
        else
        {
            for (const auto* dep : project->allDependencies)
            {
                if (dep->mergedName == "core_object")
                {
                    needsReflection = true;
                    break;
                }
            }
        }

        if (needsReflection)
        {
			auto reflectionFilePath = project->generatedPath / "reflection.cpp";

            auto* info = new GeneratedProjectFile;
            info->type = ProjectFileType::CppSource;
            info->absolutePath = reflectionFilePath;
            info->filterPath = "_generated";
            info->name = "reflection.cpp";
            project->files.push_back(info);

            project->hasReflection = true;
            project->localReflectionFile = reflectionFilePath;

            // DO NOT WRITE as it's written by the reflection tool
            //info->generatedFile = createFile(info->absolutePath);
            //valid &= generateProjectDefaultReflection(project, info->generatedFile->content);
        }
    }

    if (project->originalProject->type == ProjectType::LocalLibrary)
    {
        if (project->originalProject->flagModuleRoot)
        {
            for (const auto* sourceProject : project->originalProject->moduleSourceProjects)
            {
                auto localGlueHeader = sharedGlueFolder / sourceProject->mergedName;
                localGlueHeader += "_glue.inl";

                auto* info = new GeneratedProjectFile;
                info->absolutePath = localGlueHeader;
                info->type = ProjectFileType::CppHeader;
                info->filterPath = "_generated";
                info->name = project->mergedName + "_glue.inl";
                info->generatedFile = createFile(info->absolutePath);
                project->files.push_back(info);

                valid &= generateProjectModuleGlueFile(project, sourceProject, info->generatedFile->content);
            }
        }
        else
        {
            auto localGlueHeader = sharedGlueFolder / project->mergedName;
            localGlueHeader += "_glue.inl";

            auto* info = new GeneratedProjectFile;
            info->absolutePath = localGlueHeader;
            info->type = ProjectFileType::CppHeader;
            info->filterPath = "_generated";
            info->name = project->mergedName + "_glue.inl";
            info->generatedFile = createFile(info->absolutePath);
            project->files.push_back(info);

            valid &= generateProjectGlueFile(project, info->generatedFile->content);
        }
    }

    if (project->originalProject->type == ProjectType::LocalApplication || project->originalProject->type == ProjectType::LocalLibrary)
    {
        auto* info = new GeneratedProjectFile;
        info->absolutePath = project->generatedPath / "static_init.inl";
        info->type = ProjectFileType::CppHeader;
        info->filterPath = "_generated";
        info->name = "main.cpp";
        info->generatedFile = createFile(info->absolutePath);
        project->files.push_back(info);

        valid &= generateProjectStaticInitFile(project, info->generatedFile->content);
    }

    if (project->originalProject->type == ProjectType::LocalApplication || project->originalProject->type == ProjectType::LocalLibrary)
    {
        if (project->originalProject->flagUsePCH)
        {
            {
                auto* info = new GeneratedProjectFile;
                info->absolutePath = project->generatedPath / "build.h";
                info->type = ProjectFileType::CppHeader;
                info->filterPath = "_generated";
                info->name = "build.h";
                info->generatedFile = createFile(info->absolutePath);
                project->files.push_back(info);

                valid &= generateProjectBuildHeaderFile(project, info->generatedFile->content);
            }

            {
                auto* info = new GeneratedProjectFile;
                info->absolutePath = project->generatedPath / "build.cpp";
                info->type = ProjectFileType::CppSource;
                info->filterPath = "_generated";
                info->name = "build.cpp";
                info->generatedFile = createFile(info->absolutePath);
                project->files.push_back(info);

                valid &= generateProjectBuildSourceFile(project, info->generatedFile->content);
            }
        }
    }

    if (project->originalProject->type == ProjectType::LocalApplication)
    {
        if (project->originalProject->flagGenerateMain || project->originalProject->hasTests)
        {
            auto* info = new GeneratedProjectFile;
            info->absolutePath = project->generatedPath / "main.cpp";
            info->type = ProjectFileType::CppSource;
            info->filterPath = "_generated";
            info->name = "main.cpp";
            info->generatedFile = createFile(info->absolutePath);
            project->files.push_back(info);

            valid &= generateProjectMainSourceFile(project, info->generatedFile->content);            
        }
    }

    if (project->originalProject->type == ProjectType::EmbeddedMedia)
    {
        auto* info = new GeneratedProjectFile;
        info->absolutePath = project->generatedPath / "media_list.txt";
        info->type = ProjectFileType::MediaFileList;
        info->filterPath = "_generated";
        info->name = "media_list.txt";
        info->generatedFile = createFile(info->absolutePath);
        project->files.push_back(info);

        valid &= generateSolutionEmbeddFileList(info->generatedFile->content);
    }

	if (project->originalProject->type == ProjectType::RttiGenerator)
	{
		auto* info = new GeneratedProjectFile;
		info->absolutePath = project->generatedPath / "rtti_list.txt";
		info->type = ProjectFileType::RTTIList;
		info->filterPath = "_generated";
		info->name = "rtti_list.txt";
		info->generatedFile = createFile(info->absolutePath);
		project->files.push_back(info);

		valid &= generateSolutionReflectionFileList(info->generatedFile->content);
	}

    if (project->originalProject->type == ProjectType::LocalApplication || project->originalProject->type == ProjectType::LocalLibrary)
    {
        for (const auto* file : project->originalProject->files)
        {
            if (file->type == ProjectFileType::MediaScript)
            {
                std::string compiledMediaFile = std::string("EmbeddedMedia_") + file->originalProject->mergedName + "_" + std::string(PartBefore(file->name, ".")) + "_data.cpp";

                auto* info = new GeneratedProjectFile;
                info->absolutePath = project->generatedPath / compiledMediaFile;
                info->type = ProjectFileType::CppSource;
                info->filterPath = "_generated";
                info->name = compiledMediaFile;
                project->files.push_back(info);
            }
        }
    }    

    for (const auto& externalIncludePath : project->originalProject->externalIncludePaths)
    {
        auto fullPath = config.engineRootPath / externalIncludePath;

        try
        {
            if (fs::is_directory(fullPath))
            {
                bool hasFiles = false;
                for (const auto& entry : fs::directory_iterator(fullPath))
                {
                    const auto name = entry.path().filename().u8string();

                    if (entry.is_regular_file())
                    {
                        auto* info = new GeneratedProjectFile;
                        info->type = ProjectFileType::CppHeader;
                        info->absolutePath = fullPath / name;
                        info->absolutePath.make_preferred();
                        info->filterPath = "_shared";

                        std::cout << "Discovered shared file '" << info->absolutePath << "'\n";

                        info->name = name;
                        project->files.push_back(info);
                        hasFiles = true;
                    }
                }

                if (hasFiles)
                    project->additionalIncludePaths.push_back(fullPath);
            }
            else
            {
                std::cout << "No shared files found at shared directory '" << fullPath << "'\n";
                valid = false;
            }
        }
        catch (fs::filesystem_error& e)
        {
            std::cout << "Filesystem Error: " << e.what() << "\n";
        }
    }

    return valid;
}

bool ProjectGenerator::generateExtraCodeForProject(GeneratedProject* project)
{
    bool valid = true;

    for (size_t i = 0; i < project->files.size(); ++i)
    {
        const auto* file = project->files[i];

        if (file->type == ProjectFileType::Bison)
            valid &= processBisonFile(project, file);
    }

    // move build.cpp to the front of the list
    for (auto it = project->files.begin(); it != project->files.end(); ++it)
    {
        auto* file = *it;
        if (file->name == "build.cpp" || file->name == "build.cxx")
        {
            project->files.erase(it);
            project->files.insert(project->files.begin(), file);
            break;
        }
    }

    return valid;
}

bool ProjectGenerator::generateProjectModuleGlueFile(const GeneratedProject* project, const ProjectStructure::ProjectInfo* sourceProject, std::stringstream& f)
{
    auto macroName = ToUpper(sourceProject->mergedName) + "_GLUE";
    auto apiName = ToUpper(sourceProject->mergedName) + "_API";
    auto exportsMacroName = ToUpper(project->mergedName) + "_EXPORTS";

    writeln(f, "/***");
    writeln(f, "* Inferno Engine Glue Code");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "***/");
    writeln(f, "");    writeln(f, "");
    writeln(f, "#ifndef " + macroName);
    writeln(f, "#define " + macroName);
    writeln(f, "");

    if (!project->willBeDLL)
    {
        writeln(f, "#define " + apiName);
    }
    else
    {
        writeln(f, "  #ifdef " + exportsMacroName);
        writelnf(f, "    #define %s __declspec( dllexport )", apiName.c_str());
        writeln(f, "  #else");
        writelnf(f, "    #define %s __declspec( dllimport )", apiName.c_str());
        writeln(f, "  #endif");
    }

    if (!sourceProject->resolvedDependencies.empty())
    {
        writeln(f, "");
        writeln(f, "// Public header from project dependencies:");

        for (const auto* dep : sourceProject->resolvedDependencies)
        {
            if (dep->moduleProject != nullptr)//project->originalProject)
            {
                const auto publicHeader = dep->rootPath / "include/public.h";
                if (fs::is_regular_file(publicHeader))
                    writeln(f, "#include \"" + publicHeader.u8string() + "\"");
            }
        }
    }

    writeln(f, "#endif");

    return true;
}

bool ProjectGenerator::generateProjectGlueFile(const GeneratedProject* project, std::stringstream& f)
{
    auto macroName = ToUpper(project->mergedName) + "_GLUE";
    auto apiName = ToUpper(project->mergedName) + "_API";
    auto exportsMacroName = ToUpper(project->mergedName) + "_EXPORTS";

    writeln(f, "/***");
    writeln(f, "* Inferno Engine Glue Code");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "***/");
    writeln(f, "");    writeln(f, "");
    writeln(f, "#ifndef " + macroName);
    writeln(f, "#define " + macroName);
    writeln(f, "");

    if (!project->willBeDLL)
    {
        writeln(f, "#define " + apiName);
    }
    else
    {
        writeln(f, "  #ifdef " + exportsMacroName);
        writelnf(f, "    #define %s __declspec( dllexport )", apiName.c_str());
        writeln(f, "  #else");
        writelnf(f, "    #define %s __declspec( dllimport )", apiName.c_str());
        writeln(f, "  #endif");
    }

    if (!project->directDependencies.empty())
    {
        writeln(f, "");
        writeln(f, "// Public header from project dependencies:");

        for (const auto* dep : project->directDependencies)
            if (!dep->localPublicHeader.empty())
                writeln(f, "#include \"" + dep->localPublicHeader.u8string() + "\"");
    }

    writeln(f, "#endif");

    return true;
}

bool ProjectGenerator::generateProjectDefaultReflection(const GeneratedProject* project, std::stringstream& f)
{
    writeln(f, "/// Inferno Engine v4 by Tomasz \"RexDex\" Jonarski");
    writeln(f, "/// RTTI Glue Code Generator is under MIT License");
    writeln(f, "");
    writeln(f, "/// AUTOGENERATED FILE - ALL EDITS WILL BE LOST");
    return true;
}

bool ProjectGenerator::generateProjectMainSourceFile(const GeneratedProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");

    if (!project->originalProject->appHeaderName.empty())
        writelnf(f, "#include \"%hs\"", project->originalProject->appHeaderName.c_str());
    writelnf(f, "#include \"core/containers/include/commandLine.h\"");
    writeln(f, "");

    if (project->originalProject->hasTests)
    {
        writeln(f, "#include \"gtest/gtest.h\"");
        writeln(f, "");
    }

    bool windowsCommandLine = false;
    if (config.platform == PlatformType::Windows)
    {
        if (project->originalProject->flagConsole)
        {
            writeln(f, "int main(int argc, char** argv) {");
        }
        else
        {
            writeln(f, "#include <Windows.h>");
            writeln(f, "");
            writeln(f, "int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {");
            windowsCommandLine = true;
        }
    }
    else
    {
        writeln(f, "int main(int argc, char** argv) {");
    }

    writeln(f, "  extern void InitializeStaticDependencies();");
    writeln(f, "  InitializeStaticDependencies();");
    writeln(f, "");
    writeln(f, "  inferno::CommandLine commandLine;");
    if (windowsCommandLine)
        writeln(f, "  if (!commandLine.parse(inferno::BaseStringView<wchar_t>(pCmdLine), false))");
    else
        writeln(f, "  if (!commandLine.parse(argc, argv))");
    writeln(f, "    return -1;");
    writeln(f, "");

    if (project->originalProject->hasTests)
    {
        writeln(f, "  if (!commandLine.hasParam(\"interactive\")) {");
        writeln(f, "    #ifdef PLATFORM_LINUX");
        writeln(f, "      signal(SIGPIPE, SIG_IGN);");
        writeln(f, "    #endif");
		writeln(f, "");
        writeln(f, "    testing::InitGoogleTest(&argc, argv);");
        writeln(f, "    auto ret = RUN_ALL_TESTS();");
        writeln(f, "    if (ret) return ret;");
        writeln(f, "  }");
        writeln(f, "");
    }

    if (!project->originalProject->appClassName.empty())
    {
        writelnf(f, "  %hs app;", project->originalProject->appClassName.c_str());
        writelnf(f, "  if (!app.init(commandLine))");
        writelnf(f, "    return -1;");
        writeln(f, "");

        writeln(f, "  while (app.update()) {}");
        writeln(f, "");
    }

    writeln(f, "  return 0;");
    writeln(f, "}");

    return true;
}

bool ProjectGenerator::generateProjectBuildSourceFile(const GeneratedProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");
    writeln(f, "#include \"static_init.inl\"");
    writeln(f, "");

    if (project->hasReflection)
    {
        writelnf(f, "extern void InitializeReflection_%hs();", project->mergedName.c_str());
        writelnf(f, "extern void InitializeTests_%hs();", project->mergedName.c_str());
        writeln(f, "");
    }

    if (project->hasEmbeddedFiles)
    {
        writelnf(f, "void InitializeEmbeddedFiles_%hs() {", project->mergedName.c_str());
        for (const auto* file : project->originalProject->files)
        {
            if (file->type == ProjectFileType::MediaScript && file->originalProject)
            {
                std::string symbolName = ToUpper(file->originalProject->mergedName) + "_" + ToUpper(PartBefore(file->name, "."));
                std::string functionName = "RegisterEmbeddedFiles_" + symbolName;
                writelnf(f, "  extern void %hs();", functionName.c_str());
                writelnf(f, "  %hs();", functionName.c_str());
            }
        }
        writeln(f, "}");
        writeln(f, "");
    }

    writelnf(f, "void InitModule_%hs() {", project->mergedName.c_str());
    if (project->hasReflection && project->hasEmbeddedFiles)
        writelnf(f, "inferno::modules::TModuleInitializationFunc initFunc = []() { InitializeReflection_%hs(); InitializeTests_%hs(); InitializeEmbeddedFiles_%hs(); };", project->mergedName.c_str(), project->mergedName.c_str(), project->mergedName.c_str());
    else if (project->hasReflection)
        writelnf(f, "inferno::modules::TModuleInitializationFunc initFunc = []() { InitializeReflection_%hs(); InitializeTests_%hs(); };", project->mergedName.c_str(), project->mergedName.c_str());
    else if (project->hasEmbeddedFiles)
        writelnf(f, "inferno::modules::TModuleInitializationFunc initFunc = []() { InitializeEmbeddedFiles_%hs(); };", project->mergedName.c_str());
    else
        writelnf(f, "inferno::modules::TModuleInitializationFunc initFunc = []() { };");
    writelnf(f, "inferno::modules::RegisterModule(\"%hs\", __DATE__, __TIME__, _MSC_FULL_VER, initFunc);", project->mergedName.c_str());
    writeln(f, "}");
    writeln(f, "");

    if (project->willBeDLL)
    {
        writeln(f, "void* GModuleHandle = nullptr;");
        writeln(f, "unsigned char __stdcall DllMain(void* moduleInstance, unsigned long nReason, void*) {");
        writelnf(f, "if (nReason == 1) { GModuleHandle = moduleInstance; InitModule_%hs(); }", project->mergedName.c_str());
        writeln(f, "return 1;");
        writeln(f, "}");
        writeln(f, "");
    }

    return true;
}

bool ProjectGenerator::generateProjectBuildHeaderFile(const GeneratedProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");
    writeln(f, "#pragma once");
    writeln(f, "");

    if (project->originalProject->hasTests)
    {
        writeln(f, "#define WITH_GTEST");
        writeln(f, "");
    }

    if (project->originalProject && project->originalProject->type == ProjectType::LocalApplication && !project->allDependencies.empty())
    {
        writeln(f, "");
        writeln(f, "// Public header from project dependencies:");

        for (const auto* dep : project->allDependencies)
        {
            if (dep->originalProject->type == ProjectType::LocalLibrary)
            {
                if (dep->originalProject->flagModuleRoot || config.build != BuildType::Standalone)
                {
                    const auto publicHeader = dep->originalProject->rootPath / "include/public.h";
                    if (fs::is_regular_file(publicHeader))
                        writeln(f, "#include \"" + publicHeader.u8string() + "\"");
                }
            }
        }
    }

    for (const auto* file : project->files)
        if (file->name == "public.h")
            writeln(f, "#include \"" + file->absolutePath.u8string() + "\"");

    if (project->originalProject->hasTests)
    {
        writeln(f, "");
        writeln(f, "#include \"gtest/gtest.h\"");
    }

    /*for (const auto* file : project->files)
        if (file->name == "private.h")
            writeln(f, "#include \"" + file->absolutePath.u8string() + "\"");*/

    return true;
}

bool ProjectGenerator::projectRequiresStaticInit(const GeneratedProject* project) const
{
    if (project->originalProject->flagNoInit)
        return false;

    if (project->originalProject->type != ProjectType::LocalLibrary)
        return false;

    if (project->originalProject->flagPureDynamicLibrary)
        return false;

    for (const auto* dep : project->allDependencies)
        if (dep->mergedName == "core_system")
            return true;

    return project->mergedName == "core_system";
}

bool ProjectGenerator::generateProjectStaticInitFile(const GeneratedProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    // determine if project requires static initialization (the apps and console apps require that)
    // then pull in the library linkage, for apps we pull much more crap
    if (config.generator == GeneratorType::VisualStudio19 || config.generator == GeneratorType::VisualStudio22)
    {
        if (project->willBeLinked)
        {
            for (const auto* dep : project->originalProject->resolvedDependencies)
            {
                if (dep->type == ProjectType::ExternalLibrary)
                {
                    for (const auto& linkPath : dep->libraryLinkFile)
                    {
                        std::stringstream ss;
                        ss << linkPath;
                        writelnf(f, "#pragma comment( lib, %s )", ss.str().c_str());
                    }
                }
            }
        }
        else if (project->originalProject->type == ProjectType::LocalApplication)
        {
            std::unordered_set<const ProjectStructure::ProjectInfo*> exportedLibs;

            for (const auto* proj : project->allDependencies)
            {
                for (const auto* dep : proj->originalProject->resolvedDependencies)
                {
                    if (dep->type == ProjectType::ExternalLibrary)
                    {
                        if (exportedLibs.insert(dep).second)
                        {
                            for (const auto& linkPath : dep->libraryLinkFile)
                            {
                                std::stringstream ss;
                                ss << linkPath;
                                writelnf(f, "#pragma comment( lib, %s)", ss.str().c_str());
                            }
                        }
                    }
                }
            }
        }
    }

    // static initialization part is only generated for apps
    if (project->willHaveEntryPoint)
    {
        if (config.platform == PlatformType::Windows)
            writeln(f, "#include <Windows.h>\n\n");

        writeln(f, "void* GModuleHandle = nullptr;");

        writeln(f, "void InitializeStaticDependencies() {");

        if (config.platform == PlatformType::Windows)
            writeln(f, "    GModuleHandle = (void*)GetModuleHandle(NULL);");

        if (!project->allDependencies.empty())
        {
            // if we build based on static libraries we need to "touch" the initialization code from other modules
            for (const auto* dep : project->allDependencies)
            {
                if (projectRequiresStaticInit(dep) && dep->willBeLinked)
                {
                    if (dep->willBeDLL)
                    {
                        writelnf(f, "    inferno::modules::LoadDynamicModule(\"%s\");", dep->mergedName.c_str());
                    }
                    else
                    {
                        writelnf(f, "    extern void InitModule_%s();", dep->mergedName.c_str());
                        writelnf(f, "    InitModule_%s();", dep->mergedName.c_str());
                    }
                }
            }
        }

        // initialize ourselves
        writelnf(f, "    extern void InitModule_%s();", project->mergedName.c_str());
        writelnf(f, "    InitModule_%s();", project->mergedName.c_str());

        // initialize all module data
        writelnf(f, "    inferno::modules::InitializePendingModules();");

        writeln(f, "}");
    }

    return true;
}

bool ProjectGenerator::generateSolutionReflectionFileList(std::stringstream& f)
{
    writeln(f, NameEnumOption(config.platform));
    writeln(f, NameEnumOption(config.build));

    uint32_t numReflectedFiles = 0;
    uint32_t numTotalFiles = 0;
    for (const auto* proj : projects)
    {
        if (!proj->originalProject)
            continue;

        numTotalFiles += (uint32_t)proj->files.size();

        if (proj->hasReflection)
        {
            writelnf(f, "PROJECT");
            writelnf(f, "%hs", proj->mergedName.c_str());
            writelnf(f, "%hs", proj->localReflectionFile.u8string().c_str());

            for (const auto* file : proj->files)
            {
                if (file->type == ProjectFileType::CppSource)
                {
                    writelnf(f, "%hs", file->absolutePath.u8string().c_str());
                    numReflectedFiles += 1;
                }
            }
        }
    }

    std::cout << "Found " << numReflectedFiles << " source code files for reflection of (" << numTotalFiles << " total)\n";
    return true;
}

bool ProjectGenerator::generateSolutionEmbeddFileList(std::stringstream& f)
{
	writeln(f, NameEnumOption(config.platform));

	uint32_t numMediaFiles = 0;
	for (const auto* proj : projects)
	{
		if (!proj->hasEmbeddedFiles)
			continue;

		for (const auto* file : proj->files)
		{
			if (file->type == ProjectFileType::MediaScript)
			{
				writelnf(f, "%hs", proj->mergedName.c_str());

				writelnf(f, "%hs", file->absolutePath.u8string().c_str());

				const auto compiledMediaFileName = std::string("EmbeddedMedia_") + proj->mergedName + "_" + std::string(PartBefore(file->name, ".")) + "_data.cpp";
				const auto compiledMediaFilePath = proj->generatedPath / compiledMediaFileName;
				writelnf(f, "%hs", compiledMediaFilePath.u8string().c_str());

				numMediaFiles += 1;
			}
		}
	}

	std::cout << "Found " << numMediaFiles << " embedded media build files\n";
	return true;
}

//--

bool ProjectGenerator::processBisonFile(GeneratedProject* project, const GeneratedProjectFile* file)
{
    auto* tool = project->originalProject->findToolByName("bison");
    if (!tool)
    {
        std::cout << "BISON library not linked by current project\n";
        return false;
    }

    const auto coreName = PartBefore(file->name, ".");

    std::string parserFileName = std::string(coreName) + "_Parser.cpp";
    std::string symbolsFileName = std::string(coreName) + "_Symbols.h";
    std::string reportFileName = std::string(coreName) + "_Report.txt";

    fs::path parserFile = project->generatedPath / parserFileName;
    fs::path symbolsFile = project->generatedPath / symbolsFileName;
    fs::path reportPath = project->generatedPath / reportFileName;

    parserFile = parserFile.make_preferred();
    symbolsFile = symbolsFile.make_preferred();
    reportPath = reportPath.make_preferred();

    if (IsFileSourceNewer(file->absolutePath, parserFile))
    {
        std::error_code ec;
        if (!fs::is_directory(project->generatedPath))
        {
            if (!fs::create_directories(project->generatedPath, ec))
            {
                std::cout << "BISON tool failed because output directory \"" << project->generatedPath << "\" can't be created: " << ec << "\n";
                return false;
            }
        }

        std::stringstream params;
        params << tool->executablePath.u8string() << " ";
        params << "\"" << file->absolutePath.u8string() << "\" ";
        params << "-o\"" << parserFile.u8string() << "\" ";
        params << "--defines=\"" << symbolsFile.u8string() << "\" ";
        params << "--report-file=\"" << reportPath.u8string() << "\" ";
        params << "--verbose";

        const auto activeDir = fs::current_path();
        const auto bisonDir = tool->executablePath.parent_path();
        fs::current_path(bisonDir);

        auto code = std::system(params.str().c_str());

        fs::current_path(activeDir);

        if (code != 0)
        {
            std::cout << "BISON tool failed with exit code " << code << "\n";
            return false;
        }
        else
        {
            std::cout << "BISON tool finished and generated '" << parserFile << "'\n";
        }
    }
    else
    {
        std::cout << "BISON tool skipped because '" << parserFile << "' is up to date\n";
    }

    {
        auto* generatedFile = new GeneratedProjectFile();
        generatedFile->absolutePath = parserFile;
        generatedFile->name = parserFileName;
        generatedFile->filterPath = "_generated";
        generatedFile->type = ProjectFileType::CppSource;
        project->files.push_back(generatedFile);
    }

    {
        auto* generatedFile = new GeneratedProjectFile();
        generatedFile->absolutePath = symbolsFile;
        generatedFile->name = symbolsFileName;
        generatedFile->filterPath = "_generated";
        generatedFile->type = ProjectFileType::CppHeader;
        project->files.push_back(generatedFile);
    }

    return true;
}

//--

#if 0
private void generateGenericMainFile(GeneratedFile f) {


}
private void generateBisonOutputs(File sourcefile) {
    // get the name of the reflection file
    Path parserFile = localGeneratedPath.resolve(sourcefile.coreName + "_Parser.cpp");
    Path symbolsFile = localGeneratedPath.resolve(sourcefile.coreName + "_Symbols.h");
    Path reportPath = localGeneratedPath.resolve(sourcefile.coreName + "_Report.txt");

    // run the bison
    try {
        Path bisonPath = null;
        Path envPath = null;

        String OS = System.getProperty("os.name", "generic").toLowerCase(Locale.ENGLISH);
        if (OS.indexOf("win") >= 0) {
            bisonPath = solutionSetup.libs.resolveFileInLibraries("bison/win/bin/win_bison.exe");
            envPath = solutionSetup.libs.resolveFileInLibraries("bison/win/share/bison");
        }
        else {
            bisonPath = solutionSetup.libs.resolveFileInLibraries("bison/linux/bin/bison");
            envPath = solutionSetup.libs.resolveFileInLibraries("bison/linux/share/bison");
        }

        if (!Files.exists(bisonPath)) {
            System.err.printf("Unable to find Bison executable at %s, Bison files will NOT be build, projects may not compile\n", bisonPath.toString());
            return;
        }

        Files.createDirectories(parserFile.getParent());
        Files.createDirectories(symbolsFile.getParent());
        Files.createDirectories(reportPath.getParent());

        if (GeneratedFile.ShouldGenerateFile(sourcefile.absolutePath, parserFile)) {
            String commandLine = String.format("%s %s -o%s --defines=%s --verbose --report-file=%s", bisonPath.toString(), sourcefile.absolutePath.toString(), parserFile.toString(), symbolsFile.toString(), reportPath.toString());
            System.out.println(String.format("Running command '%s' with env '%s'", commandLine, envPath));
            Process p = Runtime.getRuntime().exec(commandLine, new String[]{ "BISON_PKGDATADIR=" + envPath.toString() });
            p.waitFor();

            // forward output
            BufferedReader stdOut = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String s = null;
            while ((s = stdOut.readLine()) != null) {
                System.out.println(s);
            }

            // forward errors
            BufferedReader stdErr = new BufferedReader(new InputStreamReader(p.getErrorStream()));
            while ((s = stdErr.readLine()) != null) {
                System.err.println(s);
            }

            if (0 != p.exitValue())
                throw new ProjectException("Unable to process BISON file " + sourcefile.shortName + ", failed with error code: " + p.exitValue());

            // tag the file with the same timestamp as the source file
            // Files.setLastModifiedTime(parserFile, Files.getLastModifiedTime(sourcefile.absolutePath));
            //Files.setLastModifiedTime(symbolsFile, Files.getLastModifiedTime(sourcefile.absolutePath));
        }
    }
    catch (Exception e) {
        e.printStackTrace();
        throw new ProjectException("Unable to start BISON", e);
    }

    // remember about the file
    localAdditionalSourceFiles.add(parserFile);
    localAdditionalHeaderFiles.add(symbolsFile);
}

private void generateGenericProjectStaticInitializationFile(GeneratedFile f) {

}
#endif

//--
