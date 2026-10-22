#include "TerrainCandidateStore.h"
#include "WorldObjectCandidate.h"
#include <fstream>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace Silver::WorldEditor
{
namespace
{
constexpr int LorenciaControlCell = 123 * TerrainSide + 135;
void CheckDirectory(std::filesystem::path path)
{
    for (; !path.empty(); path=path.parent_path())
    {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) throw std::runtime_error("No se permite guardar a traves de enlaces.");
#ifdef _WIN32
        const auto attr=GetFileAttributesW(path.c_str());
        if (attr!=INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("No se permite guardar a traves de junctions.");
#endif
        if (path==path.root_path()) break;
    }
}
void WriteVerified(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    if (std::filesystem::exists(path)) throw std::runtime_error("Ya existe un archivo candidate; no se sobrescribe.");
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream || WorldObjectCandidate::Read(path)!=bytes) throw std::runtime_error("No se pudo verificar el archivo guardado.");
}
}
TerrainCandidateStore::TerrainCandidateStore(std::filesystem::path sourceWorld, std::filesystem::path output)
    : source_(std::filesystem::absolute(sourceWorld).lexically_normal()), output_(std::filesystem::absolute(output).lexically_normal())
{
    CheckDirectory(source_);CheckDirectory(output_);
    auto relative=output_.lexically_relative(source_);
    auto reverse=source_.lexically_relative(output_);
    if (relative.empty() || *relative.begin()!=".." || reverse.empty() || *reverse.begin()!="..")
        throw std::invalid_argument("El candidate debe estar separado de la carpeta original.");
    attributeBytes_=WorldObjectCandidate::Read(source_/"EncTerrain1.att");
    heightBytes_=WorldObjectCandidate::Read(source_/"TerrainHeight.OZB");
    attributes_=TerrainAttributeCodec::Decode(attributeBytes_);
    height_=TerrainHeightFile::DecodeOzb(heightBytes_);
    if (attributes_.Cells[LorenciaControlCell]!=5) throw std::runtime_error("La celda de control de Lorencia no coincide.");
    if (std::filesystem::exists(source_/"Terrain.att"))
    {
        serverBytes_=WorldObjectCandidate::Read(source_/"Terrain.att");serverAttributes_=TerrainAttributeCodec::Decode(serverBytes_);
    }
}
TerrainBrushDocument TerrainCandidateStore::NewDocument() const
{
    TerrainBrushDocument doc;doc.Load(attributes_.Cells,height_.Pixels(),{LorenciaControlCell});return doc;
}
void TerrainCandidateStore::VerifyOriginals() const
{
    CheckDirectory(source_);CheckDirectory(output_);
    if (WorldObjectCandidate::Read(source_/"EncTerrain1.att")!=attributeBytes_
        || WorldObjectCandidate::Read(source_/"TerrainHeight.OZB")!=heightBytes_
        || (!serverBytes_.empty() && WorldObjectCandidate::Read(source_/"Terrain.att")!=serverBytes_))
        throw std::runtime_error("El mapa original cambio. No se publico un candidate.");
}
std::filesystem::path TerrainCandidateStore::Save(const TerrainBrushDocument& doc) const
{
    if (doc.Drawing()) throw std::logic_error("Solta el pincel antes de guardar.");
    VerifyOriginals();
    auto client=attributes_;client.Cells=doc.Attributes();
    const auto clientBytes=TerrainAttributeCodec::Encode(client);
    const auto heightBytes=height_.Encode(doc.Heights());
    auto server=serverAttributes_;
    if (!serverBytes_.empty())
        for (std::size_t i=0;i<server.Cells.size();++i)
        {
            const auto changed=static_cast<std::uint16_t>(client.Cells[i]^attributes_.Cells[i]);
            server.Cells[i]=static_cast<std::uint16_t>((server.Cells[i]&~changed)|(client.Cells[i]&changed));
        }
    const auto raw=serverBytes_.empty()?std::vector<std::uint8_t>{}:TerrainAttributeCodec::Encode(server);
    std::filesystem::create_directories(output_);CheckDirectory(output_);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    constexpr int Attempts=100;
    for (int attempt=0;attempt<Attempts;++attempt)
    {
        const auto name="terrain-"+std::to_string(stamp)+"-"+std::to_string(attempt);
        const auto pending=output_/(name+".partial"),final=output_/name;
        if (std::filesystem::exists(final)||!std::filesystem::create_directory(pending)) continue;
        CheckDirectory(pending);
        const auto clientDir=pending/"client";std::filesystem::create_directory(clientDir);
        WriteVerified(clientDir/"EncTerrain1.att",clientBytes);
        WriteVerified(clientDir/"TerrainHeight.OZB",heightBytes);
        WriteVerified(clientDir/"TerrainHeight.bmp",height_.ToBitmap(doc.Heights()));
        if (!raw.empty()) {std::filesystem::create_directory(pending/"server");WriteVerified(pending/"server/Terrain.att",raw);}
        std::ofstream note(pending/"README.txt");
        note<<"COPIA LAB. No se aplico al servidor ni al cliente habitual.\nClient: mapas para World1. Server: candidato ATT, requiere integracion y QA.\n";
        note.close();if(!note)throw std::runtime_error("No se completo el manifiesto descriptivo.");
        VerifyOriginals();std::filesystem::rename(pending,final);return final;
    }
    throw std::runtime_error("No se pudo reservar una nueva version de terreno.");
}
}
