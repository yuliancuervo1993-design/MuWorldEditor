#include "WorldAiCandidateCatalog.h"
#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include "json.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumCandidates = 256;
constexpr std::uintmax_t MaximumReportBytes = 2 * 1024 * 1024;

bool Linked(const std::filesystem::path& path)
{
    // std::filesystem can normalize a volume root to `F:`; StreamPath cannot
    // query that token as a Win32 file. A root is not a candidate link.
    const auto generic = path.generic_string();
    if (path.native().size() <= 3 || path == path.root_path()
        || (path.has_root_path() && path.relative_path().empty())
        || (generic.starts_with("//?/") && generic.size() <= 7)) return false;
    std::error_code error;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error))) return true;
    if (error) throw std::invalid_argument("No se pudo validar la ruta del candidato " + path.generic_string());
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.wstring().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}

void EnsureLocal(const std::filesystem::path& path)
{
    for (auto current = std::filesystem::absolute(path).lexically_normal(); !current.empty(); current = current.parent_path())
    {
        if (current == current.root_path() || current.native().size() <= 3) break;
        if (Linked(current))
            throw std::invalid_argument("La biblioteca de candidatos no sigue enlaces ni junctions.");
    }
}

Json ReadJson(const std::filesystem::path& file)
{
    EnsureLocal(file);
    if (!std::filesystem::is_regular_file(StreamPath(file))) throw std::invalid_argument("Falta el reporte del candidato.");
    const auto size = std::filesystem::file_size(StreamPath(file));
    if (size == 0 || size > MaximumReportBytes) throw std::invalid_argument("Reporte de candidato fuera de limites.");
    std::ifstream input(StreamPath(file), std::ios::binary);
    if (!input) throw std::runtime_error("No se pudo leer el reporte del candidato.");
    return Json::parse(std::string(std::istreambuf_iterator<char>(input), {}));
}

std::filesystem::path FirstFile(const std::filesystem::path& root, std::string extension)
{
    std::error_code error;
    if (!std::filesystem::is_directory(StreamPath(root), error) || error) return {};
    if (Linked(root)) return {};
    std::vector<std::filesystem::path> matches;
    for (const auto& entry : std::filesystem::directory_iterator(StreamPath(root)))
        if (entry.is_regular_file() && !Linked(entry.path())
            && WorldModelCatalog::Lower(entry.path().extension().string()) == extension)
            matches.push_back(entry.path());
    std::sort(matches.begin(), matches.end());
    return matches.empty() ? std::filesystem::path{} : matches.front();
}

std::size_t ReadTriangles(const Json& inspection)
{
    if (!inspection.contains("meshes") || !inspection.at("meshes").is_array()) return 0;
    std::size_t total = 0;
    for (const auto& mesh : inspection.at("meshes"))
        if (mesh.is_object() && mesh.contains("triangles") && mesh.at("triangles").is_number_unsigned())
        {
            const auto triangles = mesh.at("triangles").get<std::uint64_t>();
            constexpr auto Maximum = std::numeric_limits<std::size_t>::max();
            if (triangles > Maximum || total > Maximum - static_cast<std::size_t>(triangles))
                return Maximum;
            total += static_cast<std::size_t>(triangles);
        }
    return total;
}

int ReadExpectedPlacements(const Json& report)
{
    constexpr int Unavailable = -1;
    if (!report.contains("expectedPlacements")) return Unavailable;
    const auto& value = report.at("expectedPlacements");
    if (!value.is_number_integer()) return Unavailable;
    if (value.is_number_unsigned())
    {
        const auto placements = value.get<std::uint64_t>();
        if (placements > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return Unavailable;
        return static_cast<int>(placements);
    }
    const auto placements = value.get<std::int64_t>();
    if (placements < 0 || placements > std::numeric_limits<int>::max()) return Unavailable;
    return static_cast<int>(placements);
}
}

std::filesystem::path WorldAiCandidateCatalog::DefaultRoot()
{
    return std::filesystem::path(R"(F:\SilverAI\AssetCandidates)");
}

void WorldAiCandidateCatalog::Scan(const std::filesystem::path& root)
{
    root_ = root;
    assets_.clear();
    error_.clear();
    std::error_code rootError;
    if (!std::filesystem::is_directory(StreamPath(root), rootError) || rootError || Linked(root)) return;
    EnsureLocal(root);
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(StreamPath(root)))
        if (entry.is_directory() && !Linked(entry.path())) candidates.push_back(entry.path());
    std::sort(candidates.begin(), candidates.end());

    std::size_t count = 0;
    for (const auto& candidate : candidates)
    {
        if (++count > MaximumCandidates)
        {
            error_ = "La biblioteca de candidatos supera 256 carpetas; se muestran solo las primeras 256.";
            break;
        }
        const auto reportPath = candidate / "report.json";
        try
        {
            const auto report = ReadJson(reportPath);
            if (!report.is_object()) continue;
            WorldAiCandidateAsset asset;
            asset.Name = candidate.filename().string();
            asset.Root = candidate;
            asset.Bmd = FirstFile(candidate / "runtime", ".bmd");
            asset.Texture = FirstFile(candidate / "runtime", ".ozj");
            if (asset.Texture.empty()) asset.Texture = FirstFile(candidate / "runtime", ".ozt");
            if (report.contains("status") && report.at("status").is_string()) asset.Status = report.at("status").get<std::string>();
            if (report.contains("visualApproval") && report.at("visualApproval").is_string()) asset.VisualApproval = report.at("visualApproval").get<std::string>();
            const auto inspection = report.value("inspection", Json::object());
            if (inspection.is_object())
            {
                asset.Triangles = ReadTriangles(inspection);
                if (inspection.contains("structuralValidationPassed")
                    && inspection.at("structuralValidationPassed").is_boolean())
                    asset.StructuralValidation = inspection.at("structuralValidationPassed").get<bool>();
                if (inspection.contains("nativeLoaderVerified")
                    && inspection.at("nativeLoaderVerified").is_boolean())
                    asset.NativeLoaderVerified = inspection.at("nativeLoaderVerified").get<bool>();
            }
            asset.ExpectedPlacements = ReadExpectedPlacements(report);
            if (report.contains("lod") && report.at("lod").is_object())
            {
                const auto& lod = report.at("lod");
                if (lod.contains("lod0")) asset.LOD0 = lod.at("lod0").dump();
                if (lod.contains("lod1")) asset.LOD1 = lod.at("lod1").dump();
                if (lod.contains("lod2")) asset.LOD2 = lod.at("lod2").dump();
            }
            if (!asset.Bmd.empty()) assets_.push_back(std::move(asset));
        }
        catch (const std::exception& error)
        {
            if (error_.empty()) error_ = candidate.filename().string() + ": " + error.what();
        }
    }
    std::sort(assets_.begin(), assets_.end(), [](const auto& left, const auto& right) { return left.Name < right.Name; });
}
}
