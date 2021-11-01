#pragma once

#include "utils.h"
#include "project.h"

//--

struct FileGenerator
{
    struct GeneratedFile
    {
        GeneratedFile(const fs::path& path)
            : absolutePath(path)
        {}

        fs::path absolutePath;
        fs::file_time_type customtTime;

        std::stringstream content; // may be empty
    };

    GeneratedFile* createFile(const fs::path& path);

    bool saveFiles();

    //--

    std::vector<GeneratedFile*> files; // may be empty
};

//--

struct ProjectGenerator : public FileGenerator
{
    ProjectGenerator(const Configuration& config);

    struct GeneratedProjectFile
    {
        const ProjectStructure::FileInfo* originalFile = nullptr; // may be empty
        GeneratedFile* generatedFile = nullptr; // may be empty

        std::string name;
        std::string filterPath; // platform\\win
        ProjectFileType type = ProjectFileType::Unknown;

        bool useInCurrentBuild = true;

        fs::path absolutePath; // location
    };

    struct GeneratedProject;
        
    struct GeneratedGroup
    {
        std::string name;
        std::string mergedName;

        std::string assignedVSGuid;

        GeneratedGroup* parent = nullptr;
        std::vector<GeneratedGroup*> children;
        std::vector<GeneratedProject*> projects;
    };
    
    struct GeneratedProject
    {
        const ProjectStructure::ProjectInfo* originalProject;

        bool hasReflection = false;

        GeneratedGroup* group = nullptr;

        std::string mergedName;
        fs::path generatedPath; // generated/base_math/
        fs::path outputPath; // output/base_math/
        fs::path projectPath; // project/base_math/

        fs::path localGlueHeader; // _glue.inl file
        fs::path localPublicHeader; // include/public.h file

        std::vector<GeneratedProject*> directDependencies;
        std::vector<GeneratedProject*> allDependencies;

        std::vector<GeneratedProjectFile*> files; // may be empty
        std::vector<fs::path> additionalIncludePaths;

        std::string assignedVSGuid;
    };

    struct ScriptProject
    {
        const ProjectStructure::ProjectInfo* originalProject;

        std::string name; // InfernoEngine

        std::string assignedVSGuid;

        fs::path projectPath;
        fs::path projectFile;
    };

    bool extractProjects(const ProjectStructure& structure);

    bool generateAutomaticCode();

    bool generateExtraCode(); // tools

    GeneratedGroup* createGroup(std::string_view name);

    GeneratedGroup* rootGroup = nullptr;

    std::vector<GeneratedProject*> projects;

    std::vector<ScriptProject*> scriptProjects;

    std::vector<fs::path> sourceRoots;

private:
    //--

    Configuration config;

    fs::path sharedGlueFolder;

    std::unordered_map<const ProjectStructure::ProjectInfo*, GeneratedProject*> projectsMap;

    //--


    //-

    bool generateAutomaticCodeForProject(GeneratedProject* project);
    bool generateExtraCodeForProject(GeneratedProject* project);

    bool processBisonFile(GeneratedProject* project, const GeneratedProjectFile* file);

    bool generateProjectGlueFile(const GeneratedProject* project, std::stringstream& outContent);
    bool generateProjectStaticInitFile(const GeneratedProject* project, std::stringstream& outContent);
    bool generateProjectDefaultReflection(const GeneratedProject* project, std::stringstream& outContent);

    bool projectRequiresStaticInit(const GeneratedProject* project) const;

    bool shouldUseFile(const ProjectStructure::FileInfo* file) const;
    bool shouldStaticLinkProject(const GeneratedProject* project) const;

    GeneratedGroup* findOrCreateGroup(std::string_view name, GeneratedGroup* parent);

    //--

};

//--