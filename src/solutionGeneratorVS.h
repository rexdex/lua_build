#pragma once

#include "utils.h"
#include "project.h"
#include "projectGenerator.h"

//--

struct SolutionGeneratorVS
{
    SolutionGeneratorVS(const Configuration& config, ProjectGenerator& gen);

    bool generateSolution();
    bool generateProjects();

private:
    const Configuration& m_config;
    ProjectGenerator& m_gen;

    fs::path m_visualStudioScriptsPath;
    bool m_buildWithLibs = false;

    const char* m_projectVersion = nullptr;
    const char* m_toolsetVersion = nullptr;

    bool generateSourcesProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& outContent) const;
    bool generateSourcesProjectFilters(const ProjectGenerator::GeneratedProject* project, std::stringstream& outContent) const;
    bool generateSourcesProjectFileEntry(const ProjectGenerator::GeneratedProject* project, const ProjectGenerator::GeneratedProjectFile* file, std::stringstream& f) const;

    bool generateRTTIGenProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& outContent) const;
    bool generateEmbeddedMediaProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& outContent) const;

    void extractSourceRoots(const ProjectGenerator::GeneratedProject* project, std::vector<fs::path>& outPaths) const;

    void printSolutionDeclarations(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g);
    void printSolutionParentLinks(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g);

    void printSolutionScriptDeclarations(std::stringstream& f);
    void printSolutionParentScriptLinks(std::stringstream& f);
};

//--