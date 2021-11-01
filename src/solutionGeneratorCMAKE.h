#pragma once

#include "utils.h"
#include "project.h"
#include "projectGenerator.h"

//--

struct SolutionGeneratorCMAKE
{
    SolutionGeneratorCMAKE(const Configuration& config, ProjectGenerator& gen);

    bool generateSolution();
    bool generateProjects();

private:
    const Configuration& m_config;
    ProjectGenerator& m_gen;

    fs::path m_cmakeScriptsPath;
    bool m_buildWithLibs = false;

    bool generateProjectFile(const ProjectGenerator::GeneratedProject* project, std::stringstream& outContent) const;

    void extractSourceRoots(const ProjectGenerator::GeneratedProject* project, std::vector<fs::path>& outPaths) const;

    void printSolutionDeclarations(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g);    
    void printSolutionParentLinks(std::stringstream& f, const ProjectGenerator::GeneratedGroup* g);

    bool shouldStaticLinkProject(const ProjectGenerator::GeneratedProject* project) const;
};

//--