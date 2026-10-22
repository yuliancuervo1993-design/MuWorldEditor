#include "WorldSilverHdLibrary.h"
#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace Silver::WorldEditor::SilverHdLibrary
{
namespace
{
using Json = nlohmann::json;
constexpr std::uintmax_t MaximumManifestBytes = 4 * 1024 * 1024;
constexpr std::uintmax_t MaximumAssetBytes = 64 * 1024 * 1024;
constexpr std::size_t MaximumFiles = 25000;

bool Hex(const std::string& value, std::size_t length)
{
    return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

std::string Environment(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0) return {};
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void PlainTree(const std::filesystem::path& path)
{
    for (auto p = std::filesystem::absolute(path); !p.empty(); p = p.parent_path())
    {
        if (WorldModelCatalog::IsLinkedPath(p)) throw std::invalid_argument("Silver HD no admite links/junctions.");
        if (p == p.root_path()) break;
    }
    if (std::filesystem::is_regular_file(StreamPath(path))
        && std::filesystem::hard_link_count(StreamPath(path)) != 1)
        throw std::invalid_argument("Silver HD integrado requiere archivos fisicos propios.");
}

std::string Relative(const Json& row)
{
    const auto name = row.at("relative").get<std::string>();
    const auto path = Utf8Path(name);
    if (name.empty() || path.is_absolute() || PathUtf8(path.lexically_normal()) != name
        || name.find_first_of("\\:\0", 0, 3) != std::string::npos)
        throw std::invalid_argument("Ruta Silver HD invalida.");
    for (const auto& part : path)
    {
        const auto text = PathUtf8(part);
        const auto stem = WorldModelCatalog::Lower(text.substr(0, text.find('.')));
        static const std::set<std::string> devices{
            "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
            "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        if (text.empty() || text.front() == '.' || text.back() == '.' || text.back() == ' '
            || devices.contains(stem) || text.find_first_of("<>\"|?*") != std::string::npos
            || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; }))
            throw std::invalid_argument("Ruta Silver HD fuera del paquete.");
    }
    const auto extension = WorldModelCatalog::Lower(PathUtf8(path.extension()));
    static const std::set<std::string> assetExtensions{
        ".bmd", ".blend", ".smd", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".ozj", ".ozt", ".json", ".md", ".txt"};
    if (!((name.starts_with("Data/") && extension == ".bmd")
        || (name.starts_with("SilverHD/lab-art/") && assetExtensions.contains(extension))))
        throw std::invalid_argument("Archivo fuera del alcance Silver HD integrado.");
    return name;
}

void VerifyMarker(const std::filesystem::path& root)
{
    const auto path = root / "silver-world-editor.lab";
    constexpr std::string_view expected = "SILVER_WORLD_EDITOR_LAB_V1\n";
    PlainTree(path);
    if (!std::filesystem::is_regular_file(StreamPath(path))
        || std::filesystem::file_size(StreamPath(path)) != expected.size())
        throw std::invalid_argument("Biblioteca integrada fuera de sesion LAB.");
    std::ifstream input(StreamPath(path), std::ios::binary);
    std::string actual(expected.size(), '\0');
    if (!input.read(actual.data(), static_cast<std::streamsize>(actual.size())) || actual != expected)
        throw std::invalid_argument("Marcador LAB integrado distinto del contrato.");
}

Json ReadManifest(const std::filesystem::path& root)
{
    const auto manifest = root / ManifestName;
    PlainTree(manifest);
    const auto expected = Environment(HashVariable);
    if (Environment("SILVER_WORLD_EDITOR_LAB") != "1" || !Hex(expected, 64)
        || !std::filesystem::is_regular_file(StreamPath(manifest))
        || std::filesystem::file_size(StreamPath(manifest)) > MaximumManifestBytes
        || WorldModelCatalog::Lower(mu::crypto::Sha256FileHex(manifest)) != WorldModelCatalog::Lower(expected))
        throw std::invalid_argument("Falta contrato SHA de biblioteca Silver HD integrada.");
    VerifyMarker(root);
    std::ifstream input(StreamPath(manifest), std::ios::binary);
    const auto value = Json::parse(input);
    if (value.value("schema", 0) != 1 || value.value("scope", "") != "INTEGRATED_EDITOR_SILVER_HD"
        || !Hex(value.value("sourceHead", ""), 40) || !value.at("files").is_array()
        || value.at("files").empty() || value.at("files").size() > MaximumFiles)
        throw std::invalid_argument("Manifiesto Silver HD incompatible.");
    return value;
}

std::set<std::string> VerifyFiles(const std::filesystem::path& root, const Json& manifest)
{
    std::set<std::string> names, models;
    for (const auto& row : manifest.at("files"))
    {
        const auto relative = Relative(row);
        const auto hash = row.at("sha256").get<std::string>();
        if (!names.insert(WorldModelCatalog::Lower(relative)).second || !Hex(hash, 64)
            || !row.at("bytes").is_number_unsigned()) throw std::invalid_argument("Registro Silver HD invalido.");
        const auto file = root / Utf8Path(relative);
        PlainTree(file);
        const auto size = row.at("bytes").get<std::uintmax_t>();
        const bool isModel = WorldModelCatalog::Lower(PathUtf8(file.extension())) == ".bmd";
        if (size > MaximumAssetBytes || (isModel && size == 0) || !std::filesystem::is_regular_file(StreamPath(file))
            || std::filesystem::file_size(StreamPath(file)) != size
            || WorldModelCatalog::Lower(mu::crypto::Sha256FileHex(file)) != WorldModelCatalog::Lower(hash))
            throw std::invalid_argument("Archivo Silver HD distinto del manifiesto.");
        if (isModel) models.insert(relative);
    }
    if (models.empty()) throw std::invalid_argument("Biblioteca Silver HD sin modelos.");
    return models;
}
}

std::set<std::string> ValidateRoot(const std::filesystem::path& root)
{
    if (root.empty() || !root.is_absolute() || !std::filesystem::is_directory(StreamPath(root)))
        throw std::invalid_argument("La raiz Silver HD no es una carpeta absoluta valida.");
    PlainTree(root);
    if (WorldModelCatalog::Lower(PathUtf8(root.filename())) == "silver-graphics-remake") return {};
    return VerifyFiles(root, ReadManifest(root));
}
}
