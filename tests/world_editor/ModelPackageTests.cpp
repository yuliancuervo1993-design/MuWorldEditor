#include "WorldModelPackage.h"
#include "WorldModelCatalog.h"
#include "WorldModelInspection.h"
#include "ModelPackageFixture.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Silver::WorldEditor;

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << "\n";
}

void WriteBytes(const std::filesystem::path& path, std::size_t count)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (std::size_t i = 0; i < count; ++i)
        output.put(static_cast<char>(i & 0xFF));
}

void WriteModel(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    const auto bytes = Tests::PackageModelFixture();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

template<class F> void Reject(F action, const char* message)
{
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    Check(rejected,message);
}

void WriteManifest(const std::filesystem::path& root)
{
    std::ofstream output(root / "manifest.json", std::ios::binary | std::ios::trunc);
    output << "{\n"
           << "  \"schema\": \"" << WorldModelPackage::Schema << "\",\n"
           << "  \"name\": \"ImportedTree\",\n"
           << "  \"runtimeModel\": \"runtime/ImportedTree.bmd\",\n"
           << "  \"files\": [\"runtime/ImportedTree.bmd\", \"runtime/tree.ozt\"],\n"
           << "  \"missingDependencies\": []\n"
           << "}\n";
}
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--import")
    {
        try
        {
            const auto output=WorldModelPackage::ImportCandidate(argv[2],argv[3]);
            const auto entries=WorldModelPackage::Discover(argv[3]);
            Check(!entries.empty(),"real Blender package discovered after native import");
            Check(!WorldModelInspection::ReadCandidate(entries.back().Bmd).HasMissingDependencies(),"real Blender candidate geometry/textures valid");
            std::cout<<"IMPORTED "<<output.generic_string()<<"\n";return 0;
        }
        catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
    }
    const auto root = std::filesystem::current_path() / ".tools" / "model-package-tests-runtime";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    try
    {
        const auto inbox = root / "inbox";
        const auto package = inbox / "tree-package";
        WriteModel(package / "runtime" / "ImportedTree.bmd");
        WriteBytes(package / "runtime" / "tree.ozt", 16);
        WriteManifest(package);

        const auto importedRoot = root / "candidates";
        const auto imported = WorldModelPackage::ImportCandidate(package, importedRoot);

        const auto discovered = WorldModelPackage::Discover(inbox);
        Check(discovered.size() == 1, "discover import package");
        Check(discovered.front().Name == "ImportedTree", "manifest name");
        Check(discovered.front().FileCount == 2, "runtime file count");

        Check(std::filesystem::is_directory(imported), "candidate directory created");
        Check(std::filesystem::is_regular_file(imported / "runtime" / "ImportedTree.bmd"), "BMD copied");
        Check(std::filesystem::is_regular_file(imported / "runtime" / "tree.ozt"), "texture copied");
        Check(std::filesystem::is_regular_file(imported / "manifest.json"), "manifest copied");

        const auto candidates = WorldModelPackage::Discover(importedRoot);
        Check(candidates.size() == 1, "discover imported candidate");
        Check(candidates.front().Bmd.filename() == "ImportedTree.bmd", "candidate BMD resolved");

        Check(!std::filesystem::exists(root / "Data"), "test import never creates Data");

        WriteBytes(package / "runtime" / "unexpected.dll", 32);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"extra executable rejected");
        std::filesystem::remove(package / "runtime" / "unexpected.dll");
        std::filesystem::remove(package / "runtime" / "tree.ozt");
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"missing texture rejected");
        WriteBytes(package / "runtime" / "tree.ozt", 16);
        WriteBytes(package / "runtime" / "ImportedTree.bmd",32);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"malformed BMD rejected");
        WriteModel(package / "runtime" / "ImportedTree.bmd");
        const auto manifestPath=package/"manifest.json";
        nlohmann::json original;{std::ifstream stream(manifestPath);stream>>original;}
        const auto replaceManifest=[&](const auto& value){std::ofstream stream(manifestPath);stream<<value.dump();};
        auto bad=original;bad["runtimeModel"]="runtime/../outside.bmd";replaceManifest(bad);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"traversal rejected");
        bad=original;bad["files"].push_back("runtime/ImportedTree.bmd");replaceManifest(bad);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"duplicate inventory rejected");
        bad=original;bad["sha256"]={{"runtime/ImportedTree.bmd",std::string(64,'0')},{"runtime/tree.ozt",std::string(64,'0')}};replaceManifest(bad);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"tampered hash rejected");
        replaceManifest(original);
        std::filesystem::create_directory(importedRoot/"ImportedTree-2.partial");
        WriteBytes(importedRoot/"ImportedTree-2.partial"/"keep.txt",5);
        Reject([&]{WorldModelPackage::ImportCandidate(package,importedRoot);},"previous partial preserved");
        Check(std::filesystem::file_size(importedRoot/"ImportedTree-2.partial"/"keep.txt")==5,"partial unchanged");
        Check(WorldModelPackage::Discover(importedRoot).size()==1,"partial not presented as candidate");

        const auto catalogData = root / "catalog-data";
        WriteBytes(catalogData / "Object1" / "Tree01.bmd", 24);
        WriteBytes(catalogData / "Custom" / "Monster" / "LorenciaPopulation" / "Monster01.bmd", 48);
        WorldModelCatalog catalog;
        catalog.Scan(catalogData);
        Check(catalog.Assets().size() == 2, "catalog includes ObjectN and reviewed custom monster BMD");
        const auto remake = std::find_if(catalog.Assets().begin(), catalog.Assets().end(),
            [](const WorldModelAsset& asset) { return asset.Category == "Monstruos Remake"; });
        Check(remake != catalog.Assets().end(), "custom monster is categorized as Monstruos Remake");
        Check(remake->RelativePath == "Custom/Monster/LorenciaPopulation/Monster01.bmd",
            "custom monster keeps Data-relative inspection path");

        if (argc > 1)
        {
            const std::filesystem::path dataRoot(argv[1]);
            bool exportedReal = false;
            std::size_t attempted = 0;
            for (const auto& directory : std::filesystem::directory_iterator(dataRoot))
            {
                if (!directory.is_directory()
                    || !WorldModelCatalog::IsObjectFolder(directory.path().filename().string()))
                    continue;
                for (const auto& file : std::filesystem::directory_iterator(directory.path()))
                {
                    if (!file.is_regular_file()
                        || WorldModelCatalog::Lower(file.path().extension().string()) != ".bmd")
                        continue;
                    if (++attempted > 128) break;
                    try
                    {
                        const auto relative = std::filesystem::relative(file.path(), dataRoot).generic_string();
                        const auto exported = WorldModelPackage::ExportOriginal(dataRoot, relative, root / "exports");
                        Check(std::filesystem::is_regular_file(exported / "manifest.json"), "real BMD export writes manifest");
                        Check(std::filesystem::is_directory(exported / "runtime"), "real BMD export writes runtime folder");
                        exportedReal = true;
                        break;
                    }
                    catch (...) {}
                }
                if (exportedReal || attempted > 128) break;
            }
            Check(exportedReal, "export at least one real runtime BMD package");
        }

        std::filesystem::remove_all(root, ignored);
        std::cout << "ALL MODEL PACKAGE TESTS PASSED\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << "\n";
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
