#include "WorldObjectCandidate.h"
#include "WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include <algorithm>
#include <fstream>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Silver::WorldEditor
{
namespace
{
constexpr std::uintmax_t MaximumObjectFileBytes = MaximumObjects * 30 + 4;
constexpr std::uintmax_t MaximumTerrainFileBytes = 1024 * 1024;
void RefuseLinks(std::filesystem::path path)
{
    for (; !path.empty(); path = path.parent_path())
    {
        // A Windows parent walk can expose the drive root as "F:" instead of "F:\\".
        // The drive itself is outside the user-controlled path and is not a candidate
        // symlink/junction; skip that pseudo-root before asking Win32 for its status.
        if (path == path.root_name() || path == path.root_path()) break;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(StreamPath(path).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            throw std::invalid_argument("No se permiten enlaces ni junctions en las rutas del editor.");
#else
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(StreamPath(path))))
            throw std::invalid_argument("No se permiten enlaces en las rutas del editor.");
#endif
    }
}
bool Inside(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    auto left = child.generic_string(); auto right = parent.generic_string();
#ifdef _WIN32
    auto lower = [](unsigned char value) { return static_cast<char>(std::tolower(value)); };
    std::transform(left.begin(), left.end(), left.begin(), lower);
    std::transform(right.begin(), right.end(), right.begin(), lower);
#endif
    return left == right || left.starts_with(right + "/");
}
bool IsTerrainAttributeName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    constexpr std::string_view prefix = "encterrain";
    constexpr std::string_view suffix = ".att";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)
        || name.size() <= prefix.size() + suffix.size()) return false;
    const auto digits = std::string_view(name).substr(prefix.size(),
        name.size() - prefix.size() - suffix.size());
    return std::all_of(digits.begin(), digits.end(),
        [](unsigned char value) { return value >= '0' && value <= '9'; });
}
bool IsTerrainMappingName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    constexpr std::string_view prefix = "encterrain";
    constexpr std::string_view suffix = ".map";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)
        || name.size() <= prefix.size() + suffix.size()) return false;
    const auto digits = std::string_view(name).substr(prefix.size(),
        name.size() - prefix.size() - suffix.size());
    return std::all_of(digits.begin(), digits.end(),
        [](unsigned char value) { return value >= '0' && value <= '9'; });
}
void WriteNew(const std::filesystem::path& path, std::span<const std::uint8_t> data)
{
    if (std::filesystem::exists(StreamPath(path))) throw std::runtime_error("El candidate ya existe; no se sobrescribe: " + path.string());
    std::ofstream stream(StreamPath(path), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream) throw std::runtime_error("No se pudo crear el candidate: " + path.string());
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    stream.flush();
    if (!stream) throw std::runtime_error("No se completo el guardado del candidate.");
}
}
std::vector<std::uint8_t> WorldObjectCandidate::Read(const std::filesystem::path& path)
{
    RefuseLinks(path);
    const auto size = std::filesystem::file_size(StreamPath(path));
    if (size > MaximumTerrainFileBytes) throw std::invalid_argument("Archivo de mapa demasiado grande.");
    std::ifstream stream(StreamPath(path), std::ios::binary);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("No se pudo leer el archivo completo.");
    return result;
}
WorldObjectCandidate::WorldObjectCandidate(std::filesystem::path baseline, std::filesystem::path workspace)
    : baselinePath_(std::filesystem::absolute(baseline).lexically_normal()),
      workspace_(std::filesystem::absolute(workspace).lexically_normal())
{
    CheckPaths();
    originalBytes_ = Read(baselinePath_);
    if (originalBytes_.size() > MaximumObjectFileBytes) throw std::invalid_argument("Archivo OBJ demasiado grande.");
    baseline_ = WorldObjectCodec::Decode(originalBytes_);
}

std::string WorldObjectCandidate::BaselineSha256() const
{
    return mu::crypto::Sha256Hex(std::string_view(reinterpret_cast<const char*>(originalBytes_.data()),originalBytes_.size()));
}
bool WorldObjectCandidate::BaselineStillCurrent() const
{
    try{return Read(baselinePath_)==originalBytes_;}catch(...){return false;}
}

void WorldObjectCandidate::CheckPaths() const
{
    RefuseLinks(baselinePath_); RefuseLinks(workspace_);
    if (Inside(workspace_, baselinePath_.parent_path()) || Inside(baselinePath_, workspace_))
        throw std::invalid_argument("El workspace debe estar separado de la carpeta original.");
}
void WorldObjectCandidate::VerifyAssets(std::span<const WorldCandidateAsset> assets) const
{
    if (assets.size() > 5) throw std::invalid_argument("Demasiados archivos adicionales.");
    std::set<std::string> unique;
    for (const auto& asset : assets)
    {
        std::string lowerName = asset.Name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const bool baselineAsset = IsTerrainAttributeName(asset.Name)
            || IsTerrainMappingName(asset.Name)
            || lowerName == "terrainheight.ozb" || lowerName == "terrain.att";
        const bool preview = lowerName == "terrainheight-preview.bmp";
        if ((!baselineAsset && !preview) || !unique.insert(asset.Name).second || asset.Updated.empty()
            || asset.Updated.size() > MaximumTerrainFileBytes || (baselineAsset && !asset.Original.has_value()) || (preview && asset.Original.has_value()))
            throw std::invalid_argument("Archivo adicional no permitido.");
        if (asset.Original && Read(baselinePath_.parent_path() / asset.Name) != *asset.Original)
            throw std::runtime_error("El archivo original de terreno cambio; no se sobrescribe nada.");
    }
}
std::filesystem::path WorldObjectCandidate::Save(const WorldObjectFile& document, std::span<const WorldCandidateAsset> assets) const
{
    CheckPaths();
    if (document.Map != baseline_.Map || document.Encoding != baseline_.Encoding || document.Version != baseline_.Version)
        throw std::invalid_argument("No se cambia mapa ni formato durante el guardado.");
    const auto bytes = WorldObjectCodec::Encode(document);
    VerifyAssets(assets);
    if (Read(baselinePath_) != originalBytes_) throw std::runtime_error("El original cambio; guardado cancelado.");
    std::filesystem::create_directories(StreamPath(workspace_));
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::filesystem::path pending, final;
    constexpr int MaximumAttempts = 100;
    for (int attempt = 0; attempt < MaximumAttempts; ++attempt)
    {
        const auto name = "candidate-" + std::to_string(timestamp) + "-" + std::to_string(attempt);
        pending = workspace_ / (name + ".partial"); final = workspace_ / name;
        if (std::filesystem::exists(StreamPath(final)) || !std::filesystem::create_directory(StreamPath(pending))) continue;
        RefuseLinks(pending);
        const auto output = pending / baselinePath_.filename();
        WriteNew(output, bytes);
        for (const auto& asset : assets)
        {
            const auto target = pending / asset.Name;
            WriteNew(target, asset.Updated);
            if (Read(target) != asset.Updated) throw std::runtime_error("Archivo de terreno incompleto; no se promueve.");
        }
        VerifyAssets(assets);
        if (Read(output) != bytes || Read(baselinePath_) != originalBytes_)
            throw std::runtime_error("Verificacion fallida: se conserva .partial y no se promueve.");
        std::filesystem::rename(StreamPath(pending), StreamPath(final));
        return final / baselinePath_.filename();
    }
    throw std::runtime_error("No se pudo reservar una version nueva del candidate.");
}
}
