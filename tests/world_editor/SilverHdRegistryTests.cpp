#include "WorldSilverHdRegistry.h"
#include "WorldSilverHdLibrary.h"
#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << "\n";
}

void Write(const std::filesystem::path& path, std::size_t bytes = 32, unsigned seed = 0)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (std::size_t index = 0; index < bytes; ++index)
        output.put(static_cast<char>((index * 31 + seed) & 0xFF));
}
}

namespace
{
void SetEnvironment(const char* key, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
std::string ReadEnvironment(const char* key)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t bytes = 0;
    if (_dupenv_s(&value, &bytes, key) != 0) return {};
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(key);
    return value ? value : "";
#endif
}
void TestIntegratedRoot(const std::filesystem::path& root)
{
    using Json = nlohmann::json;
    const auto previousHash = ReadEnvironment(SilverHdLibrary::HashVariable);
    const auto previousLab = ReadEnvironment("SILVER_WORLD_EDITOR_LAB");
    const std::string original = "Data/Object1/Tree01.bmd";
    const std::string remake = "SilverHD/lab-art/trees/Tree01_Silver_LOD0.bmd";
    Write(root / original, 32, 1);
    Write(root / remake, 32, 2);
    const auto writeMarker = [&](const char* value) {
        std::ofstream(root / "silver-world-editor.lab", std::ios::binary | std::ios::trunc) << value;
    };
    writeMarker("SILVER_WORLD_EDITOR_LAB_V1\n");
    Json manifest{{"schema", 1}, {"scope", "INTEGRATED_EDITOR_SILVER_HD"}, {"sourceHead", std::string(40, 'a')}, {"files", Json::array()}};
    for (const auto& relative : {original, remake})
        manifest["files"].push_back({{"relative", relative}, {"bytes", std::filesystem::file_size(root / relative)},
            {"sha256", mu::crypto::Sha256FileHex(root / relative)}});
    const auto path = root / SilverHdLibrary::ManifestName;
    const auto seal = [&] {
        std::ofstream(path) << manifest.dump(2) << "\n";
        SetEnvironment(SilverHdLibrary::HashVariable, mu::crypto::Sha256FileHex(path));
    };
    SetEnvironment("SILVER_WORLD_EDITOR_LAB", "1");
    seal();
    WorldSilverHdRegistry registry;
    registry.Scan(root, root / "Data");
    Check(registry.Assets().size() == 2, "integrated library reads only sealed BMD records");
    Check(registry.Count(WorldSilverHdState::Original) == 1, "integrated Data retains original classification");
    Check(registry.Count(WorldSilverHdState::Lod) == 1, "integrated curated library retains LOD classification");
    Check(registry.CountRuntime(WorldSilverHdRuntimeState::Exact) == 1, "integrated original compares with own runtime");
    const auto refused = [&] {
        try { registry.Scan(root, root / "Data"); return false; }
        catch (const std::exception&) { return true; }
    };
    writeMarker("SILVER_WORLD_EDITOR_LAB_V1\nEXTRA\n");
    Check(refused(), "marker requires exact bytes, not just a first line");
    writeMarker("SILVER_WORLD_EDITOR_LAB_V1\n");
    SetEnvironment(SilverHdLibrary::HashVariable, std::string(64, '0'));
    Check(refused(), "unsealed integrated manifest rejected");
    seal();
    Write(root / remake, 32, 3);
    Check(refused(), "modified curated BMD rejected");
    Write(root / remake, 32, 2);
    manifest["files"][0]["relative"] = "../outside.bmd";
    seal();
    Check(refused(), "manifest traversal rejected");
    manifest["files"][0]["relative"] = original;
    manifest["files"].push_back(manifest["files"][0]);
    seal();
    Check(refused(), "duplicate paths rejected");
    SetEnvironment(SilverHdLibrary::HashVariable, previousHash);
    SetEnvironment("SILVER_WORLD_EDITOR_LAB", previousLab);
}
void TestDeliveredIntegratedRoot(const std::filesystem::path& root)
{
    std::ifstream input(root / SilverHdLibrary::ManifestName);
    const auto manifest = nlohmann::json::parse(input);
    std::size_t models = 0, originals = 0;
    for (const auto& row : manifest.at("files"))
    {
        const auto name = row.at("relative").get<std::string>();
        if (WorldModelCatalog::Lower(PathUtf8(Utf8Path(name).extension())) != ".bmd") continue;
        ++models;
        if (name.starts_with("Data/")) ++originals;
    }
    WorldSilverHdRegistry registry;
    registry.Scan(root, root / "Data");
    Check(registry.Assets().size() == models, "delivered integrated library indexes every sealed BMD");
    Check(originals > 0 && registry.CountRuntime(WorldSilverHdRuntimeState::Exact) >= originals,
        "delivered Data BMD matches its own runtime without another worktree");
    Check(registry.HashFailureCount() == 0, "delivered integrated model hashes verified");
    Check(registry.Count(WorldSilverHdState::Remake) > 0, "delivered integrated remakes retained");
    Check(registry.Count(WorldSilverHdState::Lod) > 0, "delivered integrated LOD comparison retained");
    Check(registry.Count(WorldSilverHdState::Candidate) > 0, "delivered integrated candidate comparison retained");
    Check(registry.Count(WorldSilverHdState::Rollback) > 0, "delivered integrated rollback comparison retained");
    std::cout << "INTEGRATED SILVER HD " << models << " BMD | " << originals << " runtime originals\n";
}
}

int main(int argc, char** argv)
{
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto base = std::filesystem::current_path() / (".silver-hd-registry-tests-" + nonce);
    const auto root = base / "silver-graphics-remake";
    const auto runtimeData = base / "runtime-data";
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);

    try
    {
        Write(root / "src/bin/Data/Object1/Tree01.bmd", 32, 1);
        Write(root / "src/bin/Data/Custom/World/Lorencia/Tree01_Silver.bmd", 32, 2);
        Write(root / "lab-art/lorencia-geometry/trees-v1/Tree01_Silver_LOD0.bmd", 32, 3);
        Write(root / "lab-art/lorencia-geometry/trees-v1/Tree01_Silver_LOD1.bmd", 32, 4);
        Write(root / "lab-art/lorencia-geometry/trees-v1/Tree01_Silver_LOD2.bmd", 32, 5);
        Write(root / "lab-art/great-dragon/runtime-candidate-v40/ArmorMale22.bmd", 32, 6);
        Write(root / "lab-art/great-dragon/runtime-candidate-v40/rollback/ArmorMale22.original.bmd", 32, 6);
        Write(root / "work/before-test/Object01.bmd", 32, 7);
        Write(root / "misc/Other.bmd", 32, 8);

        Write(runtimeData / "Object1/Tree01.bmd", 32, 1);
        Write(runtimeData / "Object22/ArmorMale22.bmd", 32, 9);

        WorldSilverHdRegistry registry;
        registry.Scan(root, runtimeData);

        Check(registry.Assets().size() == 9, "synthetic Silver HD assets indexed");
        Check(registry.Count(WorldSilverHdState::Original) == 1, "original classified");
        Check(registry.Count(WorldSilverHdState::Remake) == 1, "remake classified");
        Check(registry.Count(WorldSilverHdState::Lod) == 3, "LOD variants classified");
        Check(registry.Count(WorldSilverHdState::Candidate) == 1, "candidate classified");
        Check(registry.Count(WorldSilverHdState::Rollback) == 2, "rollback classified");
        Check(registry.Count(WorldSilverHdState::Unclassified) == 1, "unclassified preserved");
        Check(registry.UniquePayloadCount() == 8, "exact SHA duplicates deduplicated");
        Check(registry.HashFailureCount() == 0, "all synthetic hashes validated");
        Check(registry.CountRuntime(WorldSilverHdRuntimeState::Exact) == 1, "one exact runtime payload matched");
        Check(registry.CountRuntime(WorldSilverHdRuntimeState::Variant) == 6, "logical runtime variants distinguished");
        Check(registry.CountRuntime(WorldSilverHdRuntimeState::Missing) == 2, "missing runtime assets identified");
        Check(registry.CountRuntime(WorldSilverHdRuntimeState::Unchecked) == 0, "synthetic runtime validation complete");

        const auto tree = std::find_if(registry.Assets().begin(), registry.Assets().end(),
            [](const auto& asset) { return asset.RelativePath == "src/bin/Data/Object1/Tree01.bmd"; });
        Check(tree != registry.Assets().end(), "original tree found");
        Check(tree->LogicalKey == "tree01", "Silver suffix and LOD normalize to tree logical key");
        Check(tree->VariantCount == 5, "tree logical variants grouped");
        Check(tree->Sha256.size() == 64, "tree SHA-256 recorded");
        Check(tree->RuntimeState == WorldSilverHdRuntimeState::Exact, "original tree matches runtime byte-for-byte");

        const auto lod1 = std::find_if(registry.Assets().begin(), registry.Assets().end(),
            [](const auto& asset) { return asset.Lod == "LOD1"; });
        Check(lod1 != registry.Assets().end(), "LOD1 label extracted");

        const auto armor = std::find_if(registry.Assets().begin(), registry.Assets().end(),
            [](const auto& asset) { return asset.Name == "ArmorMale22"; });
        Check(armor != registry.Assets().end() && armor->VariantCount == 2,
            "candidate and rollback share logical variant group");
        Check(armor != registry.Assets().end() && armor->DuplicateCount == 2,
            "candidate and rollback exact duplicate detected by SHA");
        Check(armor != registry.Assets().end() && armor->RuntimeState == WorldSilverHdRuntimeState::Variant,
            "candidate differs from runtime payload with same logical key");

        TestIntegratedRoot(base / "integrated-runtime");

        if (argc > 2 && std::string(argv[1]) == "--integrated")
            TestDeliveredIntegratedRoot(std::filesystem::path(argv[2]));
        else if (argc > 1)
        {
            WorldSilverHdRegistry runtime;
            runtime.Scan(std::filesystem::path(argv[1]),
                argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path{});
            Check(runtime.Assets().size() >= 6000, "real Silver HD registry sees thousands of BMD files");
            Check(runtime.Count(WorldSilverHdState::Lod) >= 100, "real Silver HD registry sees LOD variants");
            Check(runtime.Count(WorldSilverHdState::Rollback) >= 1, "real Silver HD registry sees rollback assets");
            Check(runtime.Count(WorldSilverHdState::Candidate) >= 1, "real Silver HD registry sees candidate assets");
            Check(runtime.UniquePayloadCount() > 0 && runtime.UniquePayloadCount() <= runtime.Assets().size(),
                "real Silver HD SHA dedup inventory valid");
            if (argc > 2)
                Check(runtime.CountRuntime(WorldSilverHdRuntimeState::Exact) >= 1,
                    "real Silver HD has exact runtime SHA matches");
            std::cout << "REAL SILVER HD "
                      << runtime.Assets().size() << " BMD | "
                      << runtime.UniquePayloadCount() << " SHA UNIQUE | "
                      << runtime.Count(WorldSilverHdState::Original) << " ORIGINAL | "
                      << runtime.Count(WorldSilverHdState::Remake) << " REMAKE | "
                      << runtime.Count(WorldSilverHdState::Lod) << " LOD | "
                      << runtime.Count(WorldSilverHdState::Candidate) << " CANDIDATE | "
                      << runtime.Count(WorldSilverHdState::Rollback) << " ROLLBACK | "
                      << runtime.Count(WorldSilverHdState::Unclassified) << " SIN CLASIFICAR | "
                      << runtime.CountRuntime(WorldSilverHdRuntimeState::Exact) << " RUNTIME EXACT | "
                      << runtime.CountRuntime(WorldSilverHdRuntimeState::Variant) << " RUNTIME VARIANT | "
                      << runtime.CountRuntime(WorldSilverHdRuntimeState::Missing) << " RUNTIME MISSING\n";
        }

        std::filesystem::remove_all(base, ignored);
        std::cout << "ALL SILVER HD REGISTRY TESTS PASSED\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << "\n";
        std::filesystem::remove_all(base, ignored);
        return 1;
    }
}
