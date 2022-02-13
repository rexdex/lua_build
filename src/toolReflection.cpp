#include "common.h"
#include "toolReflection.h"
#include "projectGenerator.h"


//--

ProjectReflection::~ProjectReflection()
{

}

static bool ProjectsNeedsReflectionUpdate(const fs::path& reflectionFile, const std::vector<ProjectReflection::RefelctionFile*>& files, fs::file_time_type& outNewstTimestamp)
{
    if (files.empty())
        return true;

    try
    {
        if (!fs::is_regular_file(reflectionFile))
            return true;

        const auto reflectionFileTimestamp = fs::last_write_time(reflectionFile);
        //std::cout << "Reflection timestamp for " << reflectionFile << ": " << reflectionFileTimestamp.time_since_epoch().count() << "\n";

        outNewstTimestamp = reflectionFileTimestamp;

        bool hasChangedDetected = false;
        for (const auto* file : files)
        {
            const auto sourceFileTimestamp = fs::last_write_time(file->absoluitePath);
            //std::cout << "Timestamp for source " << file->file->absolutePath << ": " << sourceFileTimestamp.time_since_epoch().count() << "\n";

            if (sourceFileTimestamp > reflectionFileTimestamp)
            {
                if (sourceFileTimestamp > outNewstTimestamp)
                    outNewstTimestamp = sourceFileTimestamp;

                hasChangedDetected = true;
            }
        }


        if (hasChangedDetected)
        {
            std::cout << "Some files used to build " << reflectionFile << " changed, reflection will have to be refreshed\n";
            return true;
        }

        return false;
    }
    catch (...)
    {
        return true;
    }
}

bool ProjectReflection::extract(const fs::path& fileList)
{
    try
    {
        std::ifstream file(fileList);

        {
            std::string str;
            std::getline(file, str);
            std::getline(file, str);            
        }

        RefelctionProject* project = nullptr;

        std::string str;
        uint32_t numFiles = 0;
        while (std::getline(file, str))
        {
            if (str == "PROJECT")
            {
                project = new RefelctionProject();
                projects.push_back(project);

                std::getline(file, project->mergedName);
                std::getline(file, str);
                project->reflectionFilePath = str;
                continue;
            }

            if (project)
            {
                auto* file = new RefelctionFile();
                file->absoluitePath = str;
                project->files.push_back(file);

				numFiles += 1;
            }
        }

        std::cout << "Loaded " << numFiles << " files from " << projects.size() << " for reflection\n";
    }
    catch (const std::exception& e)
    {
        std::cout << "Error parsing reflection list " << e.what() << "\n";
        return false;
    }

    return true;
}


bool ProjectReflection::tokenizeFiles()
{
    bool valid = true;

    for (auto* file : files)
    {
        std::string content;
        if (LoadFileToString(file->absoluitePath, content))
        {
            valid &= file->tokenized.tokenize(content);
        }
        else
        {
            std::cout << "Failed to load content of file " << file->absoluitePath << "\n";
            valid = false;
        }
    }

    return valid;
}

bool ProjectReflection::parseDeclarations()
{
    bool valid = true;

    for (auto* file : files)
        valid &= file->tokenized.process();

    uint32_t totalDeclarations = 0;
    for (auto* file : files)
        totalDeclarations += (uint32_t)file->tokenized.declarations.size();

    std::cout << "Discovered " << totalDeclarations << " declarations\n";

    return valid;
}

bool ProjectReflection::generateReflection(FileGenerator& files) const
{
    bool valid = true;

    for (const auto* p : projects)
    {
        auto file = files.createFile(p->reflectionFilePath);
        file->customtTime = p->reflectionFileTimstamp;
        valid &= generateReflectionForProject(*p, file->content);
    }

    return valid;
}

struct ExportedDeclaration
{
    const CodeTokenizer::Declaration* declaration = nullptr;
    int priority = 0;
};

static void ExtractDeclarations(const ProjectReflection::RefelctionProject& p, std::vector<ExportedDeclaration>& outList)
{
    for (const auto* file : p.files)
    {
        for (const auto& decl : file->tokenized.declarations)
        {
            ExportedDeclaration info;
            info.declaration = &decl;

            if (decl.type == CodeTokenizer::DeclarationType::CUSTOM_TYPE)
                info.priority = 10;
            else if (decl.type == CodeTokenizer::DeclarationType::ENUM)
                info.priority = 20;
            else if (decl.type == CodeTokenizer::DeclarationType::BITFIELD)
                info.priority = 30;
            else if (decl.type == CodeTokenizer::DeclarationType::CLASS)
            {
                if (EndsWith(decl.name, "Metadata"))
                    info.priority = 40;
                else
                    info.priority = 41;
            }
            else if (decl.type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
                info.priority = 50;

            outList.push_back(info);
        }
    }

    sort(outList.begin(), outList.end(), [](const ExportedDeclaration& a, const ExportedDeclaration& b)
        {
            if (a.priority != b.priority)
                return a.priority < b.priority;

            return a.declaration->name < b.declaration->name;
        });
}

bool ProjectReflection::generateReflectionForProject(const RefelctionProject& p, std::stringstream& f) const
{
    writeln(f, "/// Inferno Engine v4 by Tomasz \"RexDex\" Jonarski");
    writeln(f, "/// RTTI Glue Code Generator is under MIT License");
    writeln(f, "");
    writeln(f, "/// AUTOGENERATED FILE - ALL EDITS WILL BE LOST");
    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");
    writeln(f, "#include \"build.h\"");

    std::vector<ExportedDeclaration> declarations;
    ExtractDeclarations(p, declarations);

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
        {
            writelnf(f, "namespace %s { extern void RegisterGlobalFunc_%s(); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
        else
        {
            writelnf(f, "namespace %s { extern void CreateType_%s(const char* name); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            writelnf(f, "namespace %s { extern void InitType_%s(); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            //writelnf(f, "namespace %s { extern void RegisterType_%s(const char* name); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }

    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    writelnf(f, "void InitializeReflection_%s()", p.mergedName.c_str());
    writeln(f, "{");
    for (const auto& d : declarations)
    {
        if (d.declaration->type != CodeTokenizer::DeclarationType::GLOBAL_FUNC)
        {
            writelnf(f, "%s::CreateType_%s(\"%s\");", 
                d.declaration->scope.c_str(), d.declaration->name.c_str(),
                d.declaration->typeName.c_str());
        }
    }

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
        {
            writelnf(f, "%s::RegisterGlobalFunc_%s();",
                d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
        else
        {
            /*writelnf(f, "%s::RegisterType_%s(\"%s::%s\"); }",
                d.declaration->scope.c_str(), d.declaration->name.c_str(),
                d.declaration->scope.c_str(), d.declaration->name.c_str());*/
            writelnf(f, "%s::InitType_%s();",
                d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }
    writeln(f, "}");

    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    writelnf(f, "void InitializeTests_%s()", p.mergedName.c_str());
    writeln(f, "{");
    writeln(f, "}");

    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    return true;
}

//--

ToolReflection::ToolReflection()
{}

bool GenerateInlinedReflection(const Configuration& config, const std::string& listPath)
{
  
}

int ToolReflection::run(const char* argv0, const Commandline& cmdline)
{
	ProjectReflection reflection;

	std::cout << "Extracting reflection data...\n";

    std::string fileListPath = cmdline.get("list");
    if (fileListPath.empty())
    {
        std::cout << "Reflection file list must be specified by -list\n";
        return -1;
    }    

	if (!reflection.extract(fileListPath))
		return -2;

	if (!reflection.tokenizeFiles())
		return -3;

	if (!reflection.parseDeclarations())
		return -4;

	std::cout << "Generating reflection files...\n";

    FileGenerator files;
	if (!reflection.generateReflection(files))
		return -5;

    if (!files.saveFiles())
        return -6;

	return 0;
}

//--