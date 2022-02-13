#pragma once

#include "utils.h"
#include "project.h"
#include "codeParser.h"

//--

struct FileGenerator;

struct ProjectReflection
{
    struct RefelctionFile
    {
        fs::path absoluitePath;
        CodeTokenizer tokenized;
    };

    struct RefelctionProject
    {
        std::string mergedName;
        std::vector<RefelctionFile*> files;
        fs::path reflectionFilePath;
        fs::file_time_type reflectionFileTimstamp;
    };

    std::vector<RefelctionFile*> files;
    std::vector<RefelctionProject*> projects;

    ~ProjectReflection();

    bool extract(const fs::path& fileList);
    bool tokenizeFiles();
    bool parseDeclarations();
    bool generateReflection(FileGenerator& files) const;

private:
    bool generateReflectionForProject(const RefelctionProject& p, std::stringstream& f) const;
};

//--

class ToolReflection
{
public:
    ToolReflection();

    int run(const char* argv0, const Commandline& cmdline);
};

//--