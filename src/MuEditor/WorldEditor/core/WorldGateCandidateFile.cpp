#include "WorldGateCandidateFile.h"
#include "WorldFileIo.h"
#include "WorldGateDocument.h"
#include "WorldModelCatalog.h"
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Silver::WorldEditor::GateCandidateFile
{
namespace
{
void PlainPath(const std::filesystem::path& path)
{
    for (auto node=std::filesystem::absolute(path); node!=node.root_path(); node=node.parent_path())
    {
        std::error_code error;
        const auto status=std::filesystem::symlink_status(StreamPath(node),error);
        if (error==std::errc::no_such_file_or_directory) continue;
        if (error) throw std::filesystem::filesystem_error("No se pudo verificar Candidate Gates",node,error);
        if (std::filesystem::is_symlink(status)) throw std::invalid_argument("Candidate Gates no sigue enlaces.");
        if (WorldModelCatalog::IsLinkedPath(node)) throw std::invalid_argument("Candidate Gates no sigue enlaces.");
    }
}
}

void Save(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    constexpr std::size_t MaximumBytes=1024*1024;
    if (path.filename()!=WorldGateDocument::CandidateFileName || bytes.empty() || bytes.size()>MaximumBytes)
        throw std::invalid_argument("Destino o contenido Gates Candidate invalido.");
    const auto pending=std::filesystem::path(path.wstring()+L".partial");
    PlainPath(path); PlainPath(pending);
    if (std::filesystem::exists(StreamPath(pending))) throw std::runtime_error("Hay un Candidate parcial; se conserva para recuperacion.");
    std::filesystem::create_directories(StreamPath(path.parent_path()));
#ifdef _WIN32
    const auto handle=CreateFileW(StreamPath(pending).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("No se pudo reservar Gates Candidate.");
    DWORD written=0;
    const bool ok=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)
        && written==bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) throw std::runtime_error("Escritura incompleta; se conservan original y parcial.");
    if (!MoveFileExW(StreamPath(pending).c_str(),StreamPath(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("No se pudo publicar Gates Candidate; original conservado.");
#else
    {
        std::ofstream stream(StreamPath(pending),std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) throw std::runtime_error("Escritura Candidate incompleta.");
    }
    std::filesystem::rename(StreamPath(pending),StreamPath(path));
#endif
}
}
