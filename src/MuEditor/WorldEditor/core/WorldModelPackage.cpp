#include "WorldModelPackage.h"
#include "WorldModelCatalog.h"
#include "WorldModelInspection.h"
#include "WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <set>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumPackageFiles = 256;
constexpr std::uintmax_t MaximumPackageFileBytes = 64 * 1024 * 1024;
constexpr std::uintmax_t MaximumPackageBytes = 256 * 1024 * 1024;

bool Linked(const std::filesystem::path& path)
{
    return WorldModelCatalog::IsLinkedPath(path);
}

void EnsureLocalTree(const std::filesystem::path& path)
{
    for (auto current = std::filesystem::absolute(path).lexically_normal(); !current.empty(); current = current.parent_path())
    {
        const auto generic = current.generic_string();
        if (current == current.root_path() || current.native().size() <= 3
            || (generic.starts_with("//?/") && generic.size() <= 7))
            break;
        if (std::filesystem::exists(StreamPath(current)) && Linked(current))
            throw std::invalid_argument("El paquete no sigue symlinks, junctions ni reparse points.");
    }
}

std::string SafeName(std::string text)
{
    for (char& ch : text)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
            ch = '_';
    while (!text.empty() && text.back() == '_') text.pop_back();
    if (text.empty()) text = "model";
    if (text.size() > 80) text.resize(80);
    return text;
}

std::filesystem::path UniqueDirectory(const std::filesystem::path& root, const std::string& base)
{
    std::filesystem::create_directories(StreamPath(root));
    EnsureLocalTree(root);
    for (int suffix = 0; suffix < 1000; ++suffix)
    {
        const auto name = suffix == 0 ? base : base + "-" + std::to_string(suffix + 1);
        const auto candidate = root / name;
        if (!std::filesystem::exists(StreamPath(candidate)))
            return candidate;
    }
    throw std::runtime_error("No se pudo reservar un nombre unico para el paquete.");
}

void CopyRegular(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    EnsureLocalTree(source);
    if (!std::filesystem::is_regular_file(StreamPath(source)) || Linked(source))
        throw std::invalid_argument("Archivo de paquete invalido: " + PathUtf8(source));
    const auto size = std::filesystem::file_size(StreamPath(source));
    if (size == 0 || size > MaximumPackageFileBytes)
        throw std::invalid_argument("Archivo de paquete fuera de limites: " + PathUtf8(source));
    std::filesystem::create_directories(StreamPath(destination.parent_path()));
    std::filesystem::copy_file(StreamPath(source), StreamPath(destination),
        std::filesystem::copy_options::overwrite_existing);
}

Json ReadManifest(const std::filesystem::path& root)
{
    EnsureLocalTree(root);
    if (!std::filesystem::is_directory(StreamPath(root)) || Linked(root))
        throw std::invalid_argument("Carpeta de paquete invalida.");
    const auto manifest = std::filesystem::exists(StreamPath(root / "editor-manifest.json"))
        ? root / "editor-manifest.json" : root / "manifest.json";
    if (!std::filesystem::is_regular_file(StreamPath(manifest)) || Linked(manifest))
        throw std::invalid_argument("Falta manifest.json en el paquete.");
    const auto size = std::filesystem::file_size(StreamPath(manifest));
    if (size == 0 || size > 1024 * 1024)
        throw std::invalid_argument("manifest.json fuera de limites.");
    std::ifstream input(StreamPath(manifest), std::ios::binary);
    if (!input) throw std::runtime_error("No se pudo leer manifest.json.");
    auto json = Json::parse(std::string(std::istreambuf_iterator<char>(input), {}));
    if (!json.is_object() || json.value("schema", "") != WorldModelPackage::Schema)
        throw std::invalid_argument("Schema de paquete no compatible.");
    return json;
}

std::filesystem::path PayloadPath(const std::string& text)
{
    const auto path = Utf8Path(text);
    const auto name = PathUtf8(path.filename());
    const auto extension = WorldModelCatalog::Lower(PathUtf8(path.extension()));
    if (path.is_absolute() || path.parent_path() != "runtime" || name.empty()
        || name == "." || name == ".." || name.back() == '.' || name.back() == ' '
        || name.find_first_of(":*?\"<>|") != std::string::npos
        || (extension != ".bmd" && extension != ".ozj" && extension != ".ozt"))
        throw std::invalid_argument("El paquete solo admite BMD/OZJ/OZT planos dentro de runtime.");
    return path;
}

Json ValidatePackage(const std::filesystem::path& root, Json manifest)
{
    const auto model = PayloadPath(manifest.at("runtimeModel").get<std::string>());
    if (WorldModelCatalog::Lower(PathUtf8(model.extension())) != ".bmd")
        throw std::invalid_argument("runtimeModel debe identificar un BMD.");
    const auto& files = manifest.at("files");
    if (!files.is_array() || files.empty() || files.size() > MaximumPackageFiles)
        throw std::invalid_argument("Inventario del paquete vacio o fuera de limites.");
    std::set<std::string> names;
    std::uintmax_t total = 0;
    Json hashes = Json::object();
    for (const auto& entry : files)
    {
        const auto path = PayloadPath(entry.get<std::string>());
        const auto key = PathUtf8(path);
        if (!names.insert(WorldModelCatalog::Lower(key)).second)
            throw std::invalid_argument("Archivo duplicado en el manifiesto.");
        EnsureLocalTree(root / path);
        const auto size = std::filesystem::file_size(StreamPath(root / path));
        if (size == 0 || size > MaximumPackageFileBytes || (total += size) > MaximumPackageBytes)
            throw std::invalid_argument("El paquete excede el presupuesto de bytes.");
        const auto hash = mu::crypto::Sha256FileHex(StreamPath(root / path));
        if (manifest.contains("sha256") && WorldModelCatalog::Lower(manifest.at("sha256").at(key).get<std::string>())
            != WorldModelCatalog::Lower(hash))
            throw std::invalid_argument("SHA-256 no coincide: " + key);
        hashes[key] = hash;
    }
    if (!names.contains(WorldModelCatalog::Lower(PathUtf8(model))))
        throw std::invalid_argument("El BMD principal no figura en el inventario.");
    EnsureLocalTree(root / "runtime");
    for (const auto& entry : std::filesystem::directory_iterator(StreamPath(root / "runtime")))
    {
        const auto key = WorldModelCatalog::Lower("runtime/" + PathUtf8(entry.path().filename()));
        if (!entry.is_regular_file() || Linked(entry.path()) || !names.contains(key))
            throw std::invalid_argument("Contenido runtime no declarado o no permitido.");
        if (WorldModelCatalog::Lower(PathUtf8(entry.path().extension())) == ".bmd"
            && WorldModelInspection::ReadCandidate(entry.path()).HasMissingDependencies())
            throw std::invalid_argument("El BMD tiene texturas faltantes o no compatibles.");
    }
    manifest["sha256"] = std::move(hashes);
    return manifest;
}

void WriteJson(const std::filesystem::path& path, const Json& json)
{
    std::ofstream output(StreamPath(path), std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("No se pudo escribir " + PathUtf8(path));
    output << json.dump(2) << "\n";
    if (!output) throw std::runtime_error("No se pudo completar " + PathUtf8(path));
}
}

std::filesystem::path WorldModelPackage::ExportRoot()
{
    return std::filesystem::path("EditorExports") / "Models";
}

std::filesystem::path WorldModelPackage::ImportInbox()
{
    return std::filesystem::path("EditorImports") / "Models";
}

std::filesystem::path WorldModelPackage::ImportedRoot()
{
    return std::filesystem::path("EditorCandidates") / "ImportedModels";
}

std::filesystem::path WorldModelPackage::ExportOriginal(
    const std::filesystem::path& dataRoot,
    const std::string& relativeModel,
    const std::filesystem::path& exportRoot)
{
    const auto inspection = WorldModelInspection::Read(dataRoot, relativeModel);
    const auto relative = Utf8Path(relativeModel);
    const auto packageName = SafeName(PathUtf8(relative.parent_path().filename()) + "-" + PathUtf8(relative.stem()));
    const auto destination = UniqueDirectory(exportRoot, packageName);
    const auto partial = destination.string() + ".partial";

    if (!std::filesystem::create_directory(StreamPath(partial)))
        throw std::runtime_error("Ya existe una exportacion parcial; se conserva sin sobrescribir.");
    const auto runtime = std::filesystem::path(partial) / "runtime";
    std::filesystem::create_directories(StreamPath(runtime));

    Json files = Json::array();
    Json missing = Json::array();

    CopyRegular(dataRoot / relative, runtime / relative.filename());
    files.push_back(PathUtf8(std::filesystem::path("runtime") / relative.filename()));

    for (const auto& texture : inspection.Textures)
    {
        if (texture.State == WorldModelInspection::TextureState::PackedFilePresent && !texture.RelativePath.empty())
        {
            const auto source = dataRoot / texture.RelativePath;
            const auto target = runtime / std::filesystem::path(texture.RelativePath).filename();
            if (!std::filesystem::exists(StreamPath(target)))
            {
                CopyRegular(source, target);
                files.push_back(PathUtf8(std::filesystem::path("runtime") / target.filename()));
            }
        }
        else
        {
            missing.push_back(texture.Name);
        }
    }

    Json manifest{
        {"schema", Schema},
        {"name", PathUtf8(relative.stem())},
        {"category", WorldModelCatalog::Category(PathUtf8(relative.stem()))},
        {"sourceRelativeModel", PathUtf8(relative)},
        {"runtimeModel", PathUtf8(std::filesystem::path("runtime") / relative.filename())},
        {"files", files},
        {"missingDependencies", missing}
    };
    WriteJson(std::filesystem::path(partial) / "manifest.json", manifest);
    std::filesystem::rename(StreamPath(partial), StreamPath(destination));
    return destination;
}

std::filesystem::path WorldModelPackage::ImportCandidate(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& importedRoot)
{
    const auto manifest = ValidatePackage(packageRoot, ReadManifest(packageRoot));
    const auto runtimeModel = Utf8Path(manifest.value("runtimeModel", ""));
    if (runtimeModel.empty() || runtimeModel.is_absolute() || PathUtf8(runtimeModel).find("..") != std::string::npos)
        throw std::invalid_argument("runtimeModel invalido.");

    const auto sourceRuntime = packageRoot / "runtime";
    if (!std::filesystem::is_directory(StreamPath(sourceRuntime)) || Linked(sourceRuntime))
        throw std::invalid_argument("Falta la carpeta runtime del paquete.");

    const auto base = SafeName(manifest.value("name", PathUtf8(packageRoot.filename())));
    const auto destination = UniqueDirectory(importedRoot, base);
    const auto partial = destination.string() + ".partial";
    if (!std::filesystem::create_directory(StreamPath(partial)))
        throw std::runtime_error("Ya existe una importacion parcial; se conserva sin sobrescribir.");
    std::filesystem::create_directory(StreamPath(std::filesystem::path(partial) / "runtime"));

    std::size_t count = 0;
    for (const auto& file : std::filesystem::directory_iterator(StreamPath(sourceRuntime)))
    {
        if (!file.is_regular_file() || Linked(file.path())) continue;
        if (++count > MaximumPackageFiles)
            throw std::length_error("El paquete supera el maximo de archivos permitido.");
        CopyRegular(file.path(), std::filesystem::path(partial) / "runtime" / file.path().filename());
    }
    if (count == 0) throw std::invalid_argument("El paquete no contiene archivos runtime.");

    const auto importedModel = std::filesystem::path(partial) / runtimeModel;
    if (!std::filesystem::is_regular_file(StreamPath(importedModel)))
        throw std::invalid_argument("El BMD declarado no existe dentro del paquete.");

    ValidatePackage(partial, manifest); // Verify copied bytes again before publishing a candidate.
    WriteJson(std::filesystem::path(partial) / "manifest.json", manifest);
    std::filesystem::rename(StreamPath(partial), StreamPath(destination));
    return destination;
}

std::vector<WorldModelPackageEntry> WorldModelPackage::Discover(const std::filesystem::path& root)
{
    std::vector<WorldModelPackageEntry> result;
    std::error_code error;
    if (!std::filesystem::is_directory(StreamPath(root), error) || error || Linked(root)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(StreamPath(root)))
    {
        if (!entry.is_directory() || Linked(entry.path()) || entry.path().extension() == ".partial") continue;
        try
        {
            const auto manifest = ReadManifest(entry.path());
            const auto runtimeModel = Utf8Path(manifest.value("runtimeModel", ""));
            if (runtimeModel.empty() || runtimeModel.is_absolute() || PathUtf8(runtimeModel).find("..") != std::string::npos)
                continue;
            const auto bmd = entry.path() / runtimeModel;
            if (!std::filesystem::is_regular_file(StreamPath(bmd)) || Linked(bmd)) continue;
            std::size_t files = 0;
            for (const auto& file : std::filesystem::directory_iterator(StreamPath(entry.path() / "runtime")))
                if (file.is_regular_file() && !Linked(file.path())) ++files;
            result.push_back({manifest.value("name", PathUtf8(entry.path().filename())), entry.path(), bmd, files});
        }
        catch (...) {}
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.Name < right.Name;
    });
    return result;
}
}
