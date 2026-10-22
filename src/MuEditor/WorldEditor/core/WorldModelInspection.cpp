#include "WorldModelInspection.h"
#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t MaximumFolderEntries = 10000;
using Paths = std::map<std::string, std::filesystem::path>;
std::filesystem::path LocalTraversalPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto value = path.wstring();
    if (value.starts_with(LR"(\\?\UNC\)")) throw std::invalid_argument("La biblioteca solo inspecciona archivos locales.");
    if (value.starts_with(LR"(\\?\)"))
    {
        constexpr std::size_t PrefixLength = 4;
        const bool drive = value.size() >= 7 && value[5] == L':' && value[6] == L'\\'
            && ((value[4] >= L'A' && value[4] <= L'Z') || (value[4] >= L'a' && value[4] <= L'z'));
        if (!drive) throw std::invalid_argument("No se inspeccionan rutas de dispositivo.");
        return std::filesystem::path(value.substr(PrefixLength));
    }
    if (value.starts_with(LR"(\\)")) throw std::invalid_argument("La biblioteca solo inspecciona archivos locales.");
#endif
    return path;
}
void CheckComponents(const std::filesystem::path& path)
{
    for (auto current = std::filesystem::absolute(LocalTraversalPath(path)); !current.empty(); current = current.parent_path())
    {
        if (current == current.root_path() || current.native().size() <= 3) break;
        if (WorldModelCatalog::IsLinkedPath(current)) throw std::invalid_argument("La biblioteca no sigue enlaces ni junctions.");
    }
}
std::filesystem::path ModelPath(const std::filesystem::path& dataRoot, const std::string& name)
{
    const auto relative = Utf8Path(name);
    const auto folder = relative.parent_path();
    if (relative.is_absolute() || name.find('\\') != std::string::npos || name.find(':') != std::string::npos
        || name.find("..") != std::string::npos || folder.empty() || folder.has_parent_path()
        || !WorldModelCatalog::IsObjectFolder(PathUtf8(folder))
        || WorldModelCatalog::Lower(PathUtf8(relative.extension())) != ".bmd")
        throw std::invalid_argument("Elegir un BMD de una carpeta ObjectN de esta biblioteca.");
    const auto path = dataRoot / relative;
    CheckComponents(path);
    if (!std::filesystem::is_regular_file(StreamPath(path))) throw std::invalid_argument("El BMD seleccionado no existe.");
    return path;
}
std::vector<std::uint8_t> ReadModel(const std::filesystem::path& path)
{
    const auto safe = StreamPath(path);
    const auto size = std::filesystem::file_size(safe);
    const auto time = std::filesystem::last_write_time(safe);
    if (size < 8 || size > WorldModelPreview::MaximumFileBytes) throw std::invalid_argument("Tamano BMD fuera de limites.");
    std::ifstream stream(safe, std::ios::binary);
    if (!stream) throw std::runtime_error("No se pudo leer el BMD seleccionado.");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) throw std::runtime_error("El BMD cambio durante la lectura.");
    CheckComponents(path);
    if (std::filesystem::file_size(safe) != size || std::filesystem::last_write_time(safe) != time)
        throw std::runtime_error("El BMD cambio durante la lectura; actualizar la biblioteca.");
    return result;
}
Paths EnumerateTextures(const std::filesystem::path& folder)
{
    Paths paths;
    std::size_t count = 0;
    for (const auto& file : std::filesystem::directory_iterator(StreamPath(folder)))
    {
        if (++count > MaximumFolderEntries) throw std::length_error("Carpeta de modelos demasiado grande para inspeccion interactiva.");
        if (WorldModelCatalog::IsLinkedPath(file.path()) || !file.is_regular_file()) continue;
        const auto name = WorldModelCatalog::Lower(PathUtf8(file.path().filename()));
        if (!paths.emplace(name, file.path()).second) throw std::runtime_error("Nombres duplicados que solo difieren en mayusculas.");
    }
    return paths;
}
bool SafeTextureName(const std::string& name)
{
    if (name.empty() || name.size() > 31 || name.find("..") != std::string::npos
        || name.back() == ' ' || name.back() == '.') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || ch == '_' || ch == '-' || ch == '.' || ch == ' ' || ch == '(' || ch == ')';
    });
}
WorldModelInspection::Texture Resolve(const std::string& name, const std::filesystem::path& folder, const Paths& paths)
{
    using State = WorldModelInspection::TextureState;
    WorldModelInspection::Texture result{name, "", State::Missing, 0};
    if (!SafeTextureName(name)) { result.State = State::UnsupportedName; return result; }
    auto packed = Utf8Path(name);
    const auto suffix = WorldModelCatalog::Lower(PathUtf8(packed.extension()));
    if (suffix == ".jpg") packed.replace_extension(".ozj");
    else if (suffix == ".tga") packed.replace_extension(".ozt");
    else { result.State = State::UnsupportedName; return result; }
    result.RelativePath = PathUtf8(folder / packed);
    const auto file = paths.find(WorldModelCatalog::Lower(PathUtf8(packed)));
    if (file != paths.end())
    {
        CheckComponents(file->second);
        result.Bytes = std::filesystem::file_size(StreamPath(file->second));
        if (result.Bytes > 0 && result.Bytes <= WorldModelPreview::MaximumFileBytes)
        {
            result.State = State::PackedFilePresent;
            result.RelativePath = PathUtf8(folder / file->second.filename());
        }
    }
    else if (paths.contains(WorldModelCatalog::Lower(name))) result.State = State::RawOnly;
    return result;
}
}
WorldModelInspection WorldModelInspection::Read(const std::filesystem::path& dataRoot, const std::string& relativeModel)
{
    const auto path = ModelPath(dataRoot, relativeModel);
    const auto bytes = ReadModel(path);
    WorldModelInspection result;
    result.RelativePath = relativeModel; result.Bytes = bytes.size();
    result.Model = WorldModelPreview::Decode(bytes);
    const auto files = EnumerateTextures(path.parent_path());
    for (const auto& texture : result.Model.Textures)
        result.Textures.push_back(Resolve(texture, Utf8Path(relativeModel).parent_path(), files));
    return result;
}
WorldModelInspection WorldModelInspection::ReadCandidate(const std::filesystem::path& bmdFile)
{
    const auto path = std::filesystem::absolute(LocalTraversalPath(bmdFile)).lexically_normal();
    CheckComponents(path);
    if (!std::filesystem::is_regular_file(StreamPath(path)))
        throw std::invalid_argument("El BMD candidato no existe.");
    const auto size = std::filesystem::file_size(StreamPath(path));
    if (size < 8 || size > WorldModelPreview::MaximumFileBytes)
        throw std::invalid_argument("Tamano BMD candidato fuera de limites.");
    const auto bytes = ReadModel(path);
    WorldModelInspection result;
    result.RelativePath = PathUtf8(path);
    result.Bytes = bytes.size();
    result.Model = WorldModelPreview::Decode(bytes);
    const auto files = EnumerateTextures(path.parent_path());
    for (const auto& texture : result.Model.Textures)
        result.Textures.push_back(Resolve(texture, path.parent_path(), files));
    return result;
}
bool WorldModelInspection::HasMissingDependencies() const
{
    return std::any_of(Textures.begin(), Textures.end(), [](const auto& texture) {
        return texture.State != TextureState::PackedFilePresent;
    });
}
}
