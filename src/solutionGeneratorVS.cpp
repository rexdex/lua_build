#include "common.h"
#include "project.h"
#include "projectGenerator.h"
#include "solutionGeneratorVS.h"

SolutionGeneratorVS::SolutionGeneratorVS(const Configuration& config, ProjectGenerator& gen)
    : m_config(config)
    , m_gen(gen)
{
    m_visualStudioScriptsPath = config.builderEnvPath / "vs";
    m_buildWithLibs = (config.libs == LibraryType::Static);

    if (config.generator == GeneratorType::VisualStudio19)
    {
        m_projectVersion = "16.0";
        m_toolsetVersion = "v142";
    }
    else if (config.generator == GeneratorType::VisualStudio22)
    {
        m_projectVersion = "17.0";
        m_toolsetVersion = "v143";
    }
}

void SolutionGeneratorVS::printSolutionDeclarations(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g)
{
    writelnf(f, "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"%s\", \"%s\", \"%s\"", g->name.c_str(), g->name.c_str(), g->assignedVSGuid.c_str());
    writeln(f, "EndProject");

    for (const auto* child : g->children)
        printSolutionDeclarations(f, child);

    for (const auto* p : g->projects)
    {
        auto projectClassGUID = "";
        auto projectFilePath = p->projectPath / p->mergedName;
        projectFilePath += ".vcxproj";

        writelnf(f, "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"%s\", \"%s\", \"%s\"", p->mergedName.c_str(), projectFilePath.u8string().c_str(), p->assignedVSGuid.c_str());
        writeln(f, "EndProject");
    }
}

void SolutionGeneratorVS::printSolutionScriptDeclarations(std::stringstream& f)
{
    if (!m_gen.scriptProjects.empty())
    {
        const auto groupText = GuidFromText("Scripts");

        writelnf(f, "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Scripts\", \"Scripts\", \"%s\"", groupText.c_str());
        writeln(f, "EndProject");

        for (const auto* p : m_gen.scriptProjects)
        {
            writelnf(f, "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"%s\", \"%s\", \"%s\"", p->name.c_str(), p->projectFile.u8string().c_str(), p->assignedVSGuid.c_str());
            writeln(f, "EndProject");
        }
    }
}

void SolutionGeneratorVS::printSolutionParentScriptLinks(std::stringstream& f)
{
    if (!m_gen.scriptProjects.empty())
    {
        const auto groupText = GuidFromText("Scripts");

        for (const auto* p : m_gen.scriptProjects)
            writelnf(f, "		%s = %s", p->assignedVSGuid.c_str(), groupText.c_str());
    }
}

void SolutionGeneratorVS::printSolutionParentLinks(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g)
{
    for (const auto* child : g->children)
    {
        // {808CE59B-D2F0-45B3-90A4-C63953B525F5} = {943E2949-809F-4411-A11F-51D51E9E579B}
        writelnf(f, "		%s = %s", child->assignedVSGuid.c_str(), g->assignedVSGuid.c_str());
        printSolutionParentLinks(f, child);
    }

    for (const auto* child : g->projects)
    {
        // {808CE59B-D2F0-45B3-90A4-C63953B525F5} = {943E2949-809F-4411-A11F-51D51E9E579B}
        writelnf(f, "		%s = %s", child->assignedVSGuid.c_str(), g->assignedVSGuid.c_str());
    }
}

static const char* NameVisualStudioConfiguration(ConfigurationType config)
{    
    switch (config)
    {
        case ConfigurationType::Checked: return "Checked";
        case ConfigurationType::Release: return "Release";
        case ConfigurationType::Debug: return "Debug";
        case ConfigurationType::Final: return "Final";
    }

    return "Release";
}

static const char* NameVisualStudioPlatform(PlatformType config)
{
    switch (config)
    {
    case PlatformType::UWP: return "UWP";
    case PlatformType::Windows: return "x64";
    case PlatformType::Prospero: return "Prospero";
    case PlatformType::Scarlett: return "Gaming.Xbox.Scarlett.x64";
    }

    return "x64";
}

bool SolutionGeneratorVS::generateSolution()
{
    const auto solutionFileName = std::string("inferno.") + m_config.mergedName() + ".sln";

    auto* file = m_gen.createFile(m_config.solutionPath / solutionFileName);
    auto& f = file->content;

    std::cout << "-------------------------------------------------------------------------------------------\n";
    std::cout << "-- SOLUTION FILE: " << file->absolutePath << "\n";
    std::cout << "-------------------------------------------------------------------------------------------\n";

    writeln(f, "Microsoft Visual Studio Solution File, Format Version 12.00");
    /*if (toolset.equals("v140")) {
        writeln(f, "# Visual Studio 14");
        writeln(f, "# Generated file, please do not modify");
        writeln(f, "VisualStudioVersion = 14.0.25420.1");
        writeln(f, "MinimumVisualStudioVersion = 10.0.40219.1");
    }
    else*/
    {
        writeln(f, "# Visual Studio 15");
        writeln(f, "# Generated file, please do not modify");
        writeln(f, "VisualStudioVersion = 15.0.26403.7");
        writeln(f, "MinimumVisualStudioVersion = 10.0.40219.1");
    }

    writeln(f, "Global");

    printSolutionDeclarations(f, m_gen.rootGroup);
    //printSolutionScriptDeclarations(f);

    {
        const auto c = NameVisualStudioConfiguration(m_config.configuration);
        const auto p = NameVisualStudioPlatform(m_config.platform);

        writeln(f, "	GlobalSection(SolutionConfigurationPlatforms) = preSolution");
        writelnf(f, "		%s|%s = %s|%s", c, p, c, p);
        writeln(f, "	EndGlobalSection");

        // project configs
        writeln(f, "	GlobalSection(ProjectConfigurationPlatforms) = postSolution");

        for (const auto* px : m_gen.projects)
        {
            if (CheckPlatformFilter(px->originalProject->filter, m_config.platform))
            {
                writelnf(f, "		%s.%s|%s.ActiveCfg = %s|%s", px->assignedVSGuid.c_str(), c, p, c, p);
                writelnf(f, "		%s.%s|%s.Build.0 = %s|%s", px->assignedVSGuid.c_str(), c, p, c, p);
            }
        }

        writeln(f, "	EndGlobalSection");
    }

    {
        writeln(f, "	GlobalSection(SolutionProperties) = preSolution");
        writeln(f, "		HideSolutionNode = FALSE");
        writeln(f, "	EndGlobalSection");
    }

    {
        writeln(f, "	GlobalSection(NestedProjects) = preSolution");
        printSolutionParentLinks(f, m_gen.rootGroup);
        printSolutionParentScriptLinks(f);
        writeln(f, "	EndGlobalSection");
    }

    writeln(f, "EndGlobal");
    return true;
}

bool SolutionGeneratorVS::generateProjects()
{
    bool valid = true;

    for (const auto* p : m_gen.projects)
    {
        if (p->originalProject->type == ProjectType::LocalLibrary || p->originalProject->type == ProjectType::LocalApplication)
        {
            {
                auto projectFilePath = p->projectPath / p->mergedName;
                projectFilePath += ".vcxproj";

                auto* file = m_gen.createFile(projectFilePath);
                valid &= generateSourcesProjectFile(p, file->content);
            }

            {
                auto projectFilePath = p->projectPath / p->mergedName;
                projectFilePath += ".vcxproj.filters";

                auto* file = m_gen.createFile(projectFilePath);
                valid &= generateSourcesProjectFilters(p, file->content);
            }
        }
        else if (p->originalProject->type == ProjectType::RttiGenerator)
        {
            {
                auto projectFilePath = p->projectPath / p->mergedName;
                projectFilePath += ".vcxproj";

                auto* file = m_gen.createFile(projectFilePath);
                valid &= generateRTTIGenProjectFile(p, file->content);
            }
        }
        else if (p->originalProject->type == ProjectType::EmbeddedMedia)
        {
            {
                auto projectFilePath = p->projectPath / p->mergedName;
                projectFilePath += ".vcxproj";

                auto* file = m_gen.createFile(projectFilePath);
                valid &= generateEmbeddedMediaProjectFile(p, file->content);
            }
        }
    }

    return true;
}

void SolutionGeneratorVS::extractSourceRoots(const ProjectGenerator::GeneratedProject* project, std::vector<fs::path>& outPaths) const
{
    for (const auto& sourceRoot : m_gen.sourceRoots)
        outPaths.push_back(sourceRoot);

    outPaths.push_back(project->originalProject->rootPath / "src");
    outPaths.push_back(project->originalProject->rootPath / "include");

    outPaths.push_back(m_config.solutionPath / "generated/_shared");
    outPaths.push_back(project->generatedPath);

    for (const auto& path : project->additionalIncludePaths)
        outPaths.push_back(path);

    for (const auto& path : project->originalProject->localIncludeDirectories)
    {
        const auto fullPath = project->originalProject->rootPath / path;
        outPaths.push_back(fullPath);
    }
}

extern void CollectDefineString(std::vector<std::pair<std::string, std::string>>& ar, std::string_view name, std::string_view value);

void CollectDefineStrings(std::vector<std::pair<std::string, std::string>>& ar, const std::vector<std::pair<std::string, std::string>>& defs)
{
    for (const auto& def : defs)
        CollectDefineString(ar, def.first, def.second);
}

bool SolutionGeneratorVS::generateSourcesProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f, "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f, "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f, "  <WindowsTargetPlatformMinVersion>10.0</WindowsTargetPlatformMinVersion>");
    writeln(f, "  <DefaultLanguage>en-US</DefaultLanguage>");

    if (m_config.platform == PlatformType::UWP)
    {
        writeln(f, "  <AppContainerApplication>true</AppContainerApplication>");
        writeln(f, "  <MinimumVisualStudioVersion>14.0</MinimumVisualStudioVersion>");
        writeln(f, "  <AppContainerApplication>true</AppContainerApplication>");
    }
    else if (m_config.platform == PlatformType::Windows)
    {
    }

    {
        f << "  <SourcesRoot>";
        std::vector<fs::path> sourceRoots;
        extractSourceRoots(project, sourceRoots);

        for (auto& root : sourceRoots)
            f << root.make_preferred().u8string() << "\\;";

        f << "</SourcesRoot>\n";
    }

    {
        f << "  <LibraryIncludePath>";
        for (const auto* lib : project->originalProject->resolvedDependencies)
            if (lib->type == ProjectType::LocalLibrary && lib->flagGlobalInclude)
                    f << (lib->rootPath / "include").u8string() << "\\;";

        for (const auto* source : project->originalProject->moduleSourceProjects)
            if (source->type == ProjectType::LocalLibrary)
                f << (source->rootPath / "include").u8string() << "\\;";

        for (const auto* lib : project->originalProject->resolvedDependencies)
            if (lib->type == ProjectType::ExternalLibrary)
                for (const auto& path : lib->libraryInlcudePaths)
                    f << path.u8string() << "\\;";
        f << "</LibraryIncludePath>\n";
    }

    const auto relativeSourceDir = fs::relative(project->originalProject->rootPath, project->originalProject->group->rootPath);

    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectGeneratedPath>%s\\</ProjectGeneratedPath>", project->generatedPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writelnf(f, " 	<ProjectSourceRoot>%s\\</ProjectSourceRoot>", project->originalProject->rootPath.u8string().c_str());
    //writelnf(f, " 	<ProjectPathName>%s</ProjectPathName>", relativeSourceDir.u8string().c_str());

    if (project->originalProject->flagNoWarnings)
        writeln(f, "     <ProjectWarningLevel>Level1</ProjectWarningLevel> ");
    else if (project->originalProject->flagWarn3)
        writeln(f, "     <ProjectWarningLevel>Level3</ProjectWarningLevel> ");

    if (project->originalProject->type == ProjectType::LocalApplication)
    {
        if (project->originalProject->flagConsole)
        {
            writeln(f, " 	<ConfigurationType>Application</ConfigurationType>");
            writeln(f, " 	<SubSystem>Windows</SubSystem>");
        }
        else
        {
            writeln(f, " 	<ConfigurationType>Application</ConfigurationType>");
            writeln(f, " 	<SubSystem>Console</SubSystem>");
        }
    }
    else if (project->originalProject->type == ProjectType::LocalLibrary)
    {
        if (project->willBeDLL)
        {
            writeln(f, " 	<ConfigurationType>DynamicLibrary</ConfigurationType>");
        }
        else
        {
            writeln(f, " 	<ConfigurationType>StaticLibrary</ConfigurationType>");
        }
    }

    f << " 	<ProjectPreprocessorDefines>$(ProjectPreprocessorDefines);";

    if (!m_buildWithLibs)
    {
        f << ToUpper(project->mergedName) << "_EXPORTS;";
        if (project->willBeDLL)
            f << ToUpper(project->mergedName) << "_DLL;";

        for (const auto* proj : project->originalProject->moduleSourceProjects)
        {
            f << ToUpper(proj->mergedName) << "_EXPORTS;";
            if (proj->flagForceSharedLibrary || m_config.libs == LibraryType::Shared)
                f << ToUpper(proj->mergedName) << "_DLL;";
        }
    }

    f << "PROJECT_NAME=" << project->mergedName << ";";

    if (project->originalProject->flagConsole)
        f  << "CONSOLE;";

    if (m_config.build == BuildType::Development)
        f << "BUILD_WITH_DEVTOOLS;";

    if (m_config.platform == PlatformType::UWP)
        f << "WINAPI_FAMILY=WINAPI_FAMILY_APP;";

    for (const auto* dep : project->allDependencies)
        f << "HAS_" << ToUpper(dep->mergedName) << ";";

    for (const auto* dep : project->allDependencies)
        if (dep->willBeDLL)
            f << ToUpper(dep->mergedName) << "_DLL;";

    if (m_buildWithLibs || !project->willBeDLL)
        f << "BUILD_AS_LIBS;";

    if (project->willBeDLL)
        f << "BUILD_DLL;";
    else
        f << "BUILD_LIB;";

    // custom defines
    {
        std::vector<std::pair<std::string, std::string>> defs;

        for (const auto* dep : project->allDependencies)
            CollectDefineStrings(defs, dep->originalProject->globalDefines);
        CollectDefineStrings(defs, project->originalProject->localDefines);

        for (const auto& def : defs)
        {
            std::cout << "Adding define '" << def.first << "' = '" << def.second << "'\n";
            if (def.second.empty())
                f << def.first << ";";
            else
                f << def.first << "=" << def.second << ";";
        }
    }    

    f << "</ProjectPreprocessorDefines>\n";

    writelnf(f, " 	<ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f, "</PropertyGroup>");

    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());

    /*long numAssemblyFiles = this.files.stream().filter(pf->pf.type == FileType.ASSEMBLY).count();
    if (numAssemblyFiles > 0) {
        writeln(f, "<ImportGroup Label=\"ExtensionSettings\" >");
        writeln(f, "  <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.props\" />");
        writeln(f, "</ImportGroup>");
    }*/

    writeln(f, "<ItemGroup>");
    for (const auto* pf : project->files)
        generateSourcesProjectFileEntry(project, pf, f);
    writeln(f, "</ItemGroup>");

    writeln(f, "<ItemGroup>");
    if (m_buildWithLibs && (project->originalProject->type == ProjectType::LocalApplication))
    {
        for (const auto* dep : project->allDependencies)
        {
            auto projectFilePath = dep->projectPath / dep->mergedName;
            projectFilePath += ".vcxproj";

            writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
            writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
            writeln(f, " </ProjectReference>");
        }
    }
    else
    {
        for (const auto* dep : project->directDependencies)
        {
            // when building static libs they are linked together at the end, we don't have to keep track of local dependencies
            if (m_buildWithLibs && dep->originalProject->type != ProjectType::RttiGenerator)
                continue;

            if (dep->originalProject->flagPureDynamicLibrary)
                continue;

            auto projectFilePath = dep->projectPath / dep->mergedName;
            projectFilePath += ".vcxproj";

            writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
            writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
            writeln(f, " </ProjectReference>");
        }
    }
    if (project->originalProject->type == ProjectType::LocalApplication)
    {
        for (const auto* dep : m_gen.scriptProjects)
        {
            auto projectFilePath = dep->projectPath / dep->name;
            projectFilePath += ".csproj";

            writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
            writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
            writelnf(f, "   <Private>false</Private>");
            writelnf(f, "   <ReferenceOutputAssembly>false</ReferenceOutputAssembly>");
            writeln(f, " </ProjectReference>");
        }
    }
    writeln(f, "</ItemGroup>");

    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");

    /*if (numAssemblyFiles > 0) {
        writeln(f, "<ImportGroup Label=\"ExtensionTargets\">");
        writeln(f, "  <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.targets\" />");
        writeln(f, "</ImportGroup>");
    }*/

    writeln(f, "</Project>");

    return true;
}

bool SolutionGeneratorVS::generateSourcesProjectFileEntry(const ProjectGenerator::GeneratedProject* project, const ProjectGenerator::GeneratedProjectFile* file, std::stringstream& f) const
{
    switch (file->type)
    {
        case ProjectFileType::CppHeader:
        {
            writelnf(f, "   <ClInclude Include=\"%s\">", file->absolutePath.u8string().c_str());
            if (!file->useInCurrentBuild)
                writeln(f, "   <ExcludedFromBuild>true</ExcludedFromBuild>");
            writeln(f, "   </ClInclude>");
            break;
        }

        case ProjectFileType::CppSource:
        {
            writelnf(f, "   <ClCompile Include=\"%s\">", file->absolutePath.u8string().c_str());

            if (project->originalProject->flagUsePCH)
            {
                if (file->name == "build.cpp" || file->name == "build.cxx")
                    writeln(f, "      <PrecompiledHeader>Create</PrecompiledHeader>");
                else if (!file->originalFile || !file->originalFile->flagUsePch || file->name == "main.cpp" || file->name == "main.cxx")
                    writeln(f, "      <PrecompiledHeader>NotUsing</PrecompiledHeader>");
            }
            else
            {
                writeln(f, "      <PrecompiledHeader>NotUsing</PrecompiledHeader>");
            }

            if (!file->useInCurrentBuild)
                writeln(f, "      <ExcludedFromBuild>true</ExcludedFromBuild>");

            if (m_config.platform == PlatformType::UWP)
            {
                const auto ext = file->absolutePath.extension().u8string();
                auto compileAsWinRT = false;
                
                if (ext == ".cxx" || project->originalProject->type == ProjectType::LocalApplication)
                    compileAsWinRT = true;

                if (compileAsWinRT)
                    writeln(f, "      <CompileAsWinRT>true</CompileAsWinRT>");
                else                
                    writeln(f, "      <CompileAsWinRT>false</CompileAsWinRT>");
            }

            if (file->originalFile)
            {
                const auto relativePath = fs::path(file->originalFile->projectRelativePath).parent_path().u8string();
                const auto fullPath = (project->outputPath / "obj" / relativePath).make_preferred();

				std::error_code ec;
				if (!fs::is_directory(fullPath, ec))
				{
					if (!fs::create_directories(fullPath, ec))
					{
						std::cout << "Failed to create solution directory " << fullPath << "\n";
						return false;
					}
				}

                writelnf(f, "      <ObjectFileName>$(IntDir)\\%s\\</ObjectFileName>", relativePath.c_str());
            }

            writeln(f, "   </ClCompile>");
            break;
        }

        case ProjectFileType::Bison:
        {
            writelnf(f, "   <None Include=\"%s\">", file->absolutePath.u8string().c_str());
            writelnf(f, "      <SubType>Bison</SubType>");
            writeln(f, "   </None>");
            break;
        }

        case ProjectFileType::WindowsResources:
        {
            writelnf(f, "   <ResourceCompile Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        case ProjectFileType::MediaFile:
        case ProjectFileType::MediaScript:
        case ProjectFileType::BuildScript:
        {
            writelnf(f, "   <None Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        case ProjectFileType::MediaFileList:
        {
            writelnf(f, "   <EmbeddScript Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        case ProjectFileType::NatVis:
        {
            writelnf(f, "   <Natvis Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        /*case VSIXMANIFEST:
        {
            f.writelnf("   <None Include=\"%s\">", pf.absolutePath);
            f.writelnf("      <SubType>Designer</SubType>");
            f.writelnf("   </None>");
            break;
        }*/

    }

    return true;
}

static void AddFilterSection(std::string_view path, std::vector<std::string>& outFilterSections)
{
    if (outFilterSections.end() == find(outFilterSections.begin(), outFilterSections.end(), path))
        outFilterSections.push_back(std::string(path));
}

static void AddFilterSectionRecursive(std::string_view path, std::vector<std::string>& outFilterSections)
{
    auto pos = path.find_last_of('\\');
    if (pos != -1)
    {
        auto left = path.substr(0, pos);
        AddFilterSectionRecursive(left, outFilterSections);
    }

    AddFilterSection(path, outFilterSections);
    
}

bool SolutionGeneratorVS::generateSourcesProjectFilters(const ProjectGenerator::GeneratedProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");
    writeln(f, "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");

    std::vector<std::string> filterSections;

    // file entries
    {
        writeln(f, "<ItemGroup>");
        for (const auto* file : project->files)
        {
            const char* filterType = "None";

            switch (file->type)
            {
                case ProjectFileType::CppHeader:
                    filterType = "ClInclude";
                    break;
                case ProjectFileType::CppSource:
                    filterType = "ClCompile";
                    break;
                case ProjectFileType::MediaFileList:
                    filterType = "EmbeddScript";
                    break;
				case ProjectFileType::NatVis:
					filterType = "NatVis";
					break;
            }

            if (file->filterPath.empty())
            {
                writelnf(f, "  <%s Include=\"%s\"/>", filterType, file->absolutePath.u8string().c_str());
            }
            else
            {
                writelnf(f, "  <%s Include=\"%s\">", filterType, file->absolutePath.u8string().c_str());
                writelnf(f, "    <Filter>%s</Filter>", file->filterPath.c_str());
                writelnf(f, "  </%s>", filterType);

                AddFilterSectionRecursive(file->filterPath, filterSections);
            }
        }           

        writeln(f, "</ItemGroup>");
    }

    // filter section
    {
        writeln(f, "<ItemGroup>");

        for (const auto& section : filterSections)
        {
            const auto guid = GuidFromText(section);

            writelnf(f, "  <Filter Include=\"%s\">", section.c_str());
            writelnf(f, "    <UniqueIdentifier>%s</UniqueIdentifier>", guid.c_str());
            writelnf(f, "  </Filter>");
        }

        writeln(f, "</ItemGroup>");
    }

    writeln(f, "</Project>");

    return true;
}

bool SolutionGeneratorVS::generateEmbeddedMediaProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f, "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f, "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f, "  <ModuleType>Empty</ModuleType>");
    writeln(f, " <SolutionType>SharedLibraries</SolutionType>");
    writelnf(f, "  <ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f, "  <DisableFastUpToDateCheck>true</DisableFastUpToDateCheck>");
    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writeln(f, "</PropertyGroup>");
    writeln(f, "  <PropertyGroup>");
    writeln(f, "    <PreBuildEventUseInBuild>true</PreBuildEventUseInBuild>");
    writeln(f, "  </PropertyGroup>");
    writeln(f, "  <ItemDefinitionGroup>");
    writeln(f, "    <PreBuildEvent>");

    auto toolPath = (m_config.deployPath / "tool_fxc.exe").u8string();
    auto toolPathStr = std::string(toolPath.c_str());

    {
        std::stringstream cmd;
        //writelnf(f, "<Error Text=\"No tool to compile embedded media found, was tool_embedd compiled properly?\" Condition=\"!Exists('$%hs')\" />", toolPath.c_str());

        for (const auto* pf : project->files)
        {
            if (pf->type == ProjectFileType::MediaFileList)
            {
                f << "      <Command>" << toolPath << " pack -input=" << pf->absolutePath.u8string() << "</Command>\n";
            }
        }
    }

    writeln(f, "    </PreBuildEvent>");
    writeln(f, "  </ItemDefinitionGroup>");
    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, "  <ItemGroup>");
    //for (const auto* pf : project->files)
      //  generateSourcesProjectFileEntry(project, pf, f);
    writeln(f, "  </ItemGroup>");
    writeln(f, "  <ItemGroup>");
    for (const auto* dep : project->directDependencies)
    {
        auto projectFilePath = dep->projectPath / dep->mergedName;
        projectFilePath += ".vcxproj";

        writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
        writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
        writeln(f, " </ProjectReference>");
    }
    writeln(f, "  </ItemGroup>");
    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");
    writeln(f, "</Project>");

    return true;
}

bool SolutionGeneratorVS::generateRTTIGenProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f,  "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f,  "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f,  "  <ModuleType>Empty</ModuleType>");
    writeln(f,   " <SolutionType>SharedLibraries</SolutionType>");
    writelnf(f, "  <ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f,  "  <DisableFastUpToDateCheck>true</DisableFastUpToDateCheck>");
    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writeln(f,  "</PropertyGroup>");
    writeln(f,  "  <PropertyGroup>");
    writeln(f,  "    <PreBuildEventUseInBuild>true</PreBuildEventUseInBuild>");
    writeln(f,  "  </PropertyGroup>");
    writeln(f,  "  <ItemDefinitionGroup>");
    writeln(f,  "    <PreBuildEvent>");

    {
        std::stringstream cmd;
        f << "      <Command>";
        f << m_config.builderExecutablePath.u8string() << " ";
        f << "-tool=reflection ";

        const auto reflectionListPath = project->generatedPath / "rtti_list.txt";
        f << "-list= \"" << reflectionListPath.u8string() << "\"";
        /*f << "-build=" << NameEnumOption(m_config.build) << " ";
        f << "-config=" << NameEnumOption(m_config.configuration) << " ";
        f << "-platform=" << NameEnumOption(m_config.platform) << " ";
        f << "-libs=" << NameEnumOption(m_config.libs) << " ";
        f << "-generator=" << NameEnumOption(m_config.generator) << " ";*/
        f << "</Command>\n";
    }

    writeln(f, "    </PreBuildEvent>");
    writeln(f, "  </ItemDefinitionGroup>");
    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, "  <ItemGroup>");
    writeln(f, "  </ItemGroup>");
    writeln(f, "  <ItemGroup>");
    writeln(f, "  </ItemGroup>");
    writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");
    writeln(f, "</Project>");

    return true;
}

//--
