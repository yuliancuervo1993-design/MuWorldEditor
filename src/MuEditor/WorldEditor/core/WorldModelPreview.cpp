#include "WorldModelPreview.h"
#include "WorldModelReader.h"
#include "WorldObjectCodec.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr int MaximumMeshes = 50;
constexpr int MaximumBones = 200;
constexpr int MaximumMeshVertices = 10000;
constexpr int MaximumMeshTriangles = 15000;
constexpr int MaximumActions = 32767;
constexpr int MaximumAnimationKeys = 32767;
constexpr std::size_t TriangleRecordBytes = 64;
constexpr std::size_t PreviewSlotsPerMesh = 4;
using Point = WorldModelPreview::Point;
using Triangle = WorldModelPreview::Triangle;
struct Mesh
{
    int Vertices = 0, Normals = 0, TexCoords = 0, Faces = 0;
    std::vector<Point> Positions;
    std::vector<int> Bones;
};
void RequireIndex(int index, int count)
{
    if (index < 0 || index >= count) throw std::invalid_argument("Indice BMD fuera de rango.");
}
Point ReadPoint(WorldModelReader& reader)
{
    return {reader.Float(), reader.Float(), reader.Float()};
}
void ReadVertices(WorldModelReader& reader, Mesh& mesh, std::vector<bool>& usedBones)
{
    mesh.Positions.reserve(static_cast<std::size_t>(mesh.Vertices));
    for (int index = 0; index < mesh.Vertices; ++index)
    {
        const auto bone = reader.SignedShort();
        RequireIndex(bone, static_cast<int>(usedBones.size()));
        usedBones[static_cast<std::size_t>(bone)] = true;
        reader.Take(2); // Vertex_t alignment padding is stored on disk.
        mesh.Positions.push_back(ReadPoint(reader));
        mesh.Bones.push_back(bone);
    }
}
void ReadNormalsAndUvs(WorldModelReader& reader, const Mesh& mesh, std::vector<bool>& usedBones)
{
    for (int index = 0; index < mesh.Normals; ++index)
    {
        const auto bone = reader.SignedShort();
        RequireIndex(bone, static_cast<int>(usedBones.size()));
        usedBones[static_cast<std::size_t>(bone)] = true;
        reader.Take(2); ReadPoint(reader);
        RequireIndex(reader.SignedShort(), mesh.Vertices);
        reader.Take(2);
    }
    for (int index = 0; index < mesh.TexCoords; ++index) { reader.Float(); reader.Float(); }
}
Triangle ReadFace(WorldModelReader& reader, const Mesh& mesh, std::array<int,3>& bones)
{
    WorldModelReader face(reader.Take(TriangleRecordBytes));
    if (face.Unsigned(1) != 3) throw std::invalid_argument("Previsualizacion disponible solo para caras trianguladas.");
    face.Take(1);
    Triangle triangle{};
    for (std::size_t corner = 0; corner < triangle.size(); ++corner)
    {
        const auto index = face.SignedShort(); RequireIndex(index, mesh.Vertices);
        triangle[corner] = mesh.Positions[static_cast<std::size_t>(index)];
        bones[corner] = mesh.Bones[static_cast<std::size_t>(index)];
    }
    face.Take(2); // Four corner slots exist; only three are used.
    for (const int count : {mesh.Normals, mesh.TexCoords})
    {
        for (std::size_t corner = 0; corner < triangle.size(); ++corner) RequireIndex(face.SignedShort(), count);
        face.Take(2);
    }
    return triangle;
}
void ReadMeshes(WorldModelReader& reader, WorldModelPreview& result, std::vector<bool>& usedBones, std::vector<std::array<int,3>>& geometryBones)
{
    const auto perMesh = std::max(PreviewSlotsPerMesh,
        WorldModelPreview::MaximumPreviewTriangles / static_cast<std::size_t>(std::max(1, result.MeshCount)));
    for (int meshIndex = 0; meshIndex < result.MeshCount; ++meshIndex)
    {
        Mesh mesh;
        mesh.Vertices = reader.Count(MaximumMeshVertices);
        mesh.Normals = reader.Count(MaximumMeshVertices);
        mesh.TexCoords = reader.Count(MaximumMeshVertices);
        mesh.Faces = reader.Count(MaximumMeshTriangles);
        RequireIndex(reader.SignedShort(), result.MeshCount);
        ReadVertices(reader, mesh, usedBones);
        ReadNormalsAndUvs(reader, mesh, usedBones);
        const auto step = std::max(std::size_t{1}, (static_cast<std::size_t>(mesh.Faces) + perMesh - 1) / perMesh);
        for (int index = 0; index < mesh.Faces; ++index)
        {
            std::array<int,3> bones{};
            const auto triangle = ReadFace(reader, mesh, bones);
            if (static_cast<std::size_t>(index) % step == 0 && result.Geometry.size() < WorldModelPreview::MaximumPreviewTriangles)
                { result.Geometry.push_back(triangle); geometryBones.push_back(bones); }
        }
        result.Textures.push_back(reader.Name());
        result.VertexCount += static_cast<std::size_t>(mesh.Vertices);
        result.TriangleCount += static_cast<std::size_t>(mesh.Faces);
    }
}
std::vector<int> ReadActions(WorldModelReader& reader, int count)
{
    std::vector<int> keys; keys.reserve(static_cast<std::size_t>(count));
    for (int action = 0; action < count; ++action)
    {
        const auto keyCount = reader.Count(MaximumAnimationKeys);
        const bool locked = reader.Boolean();
        if (locked) for (int key = 0; key < keyCount; ++key) ReadPoint(reader);
        keys.push_back(keyCount);
    }
    return keys;
}
using Matrix = std::array<std::array<float,4>,3>;
Matrix LocalPose(const Point& position, const Point& rotation)
{
    // Same Rz * Ry * Rx convention as the native AngleQuaternion/QuaternionMatrix.
    const float sx=std::sin(rotation[0]),cx=std::cos(rotation[0]);
    const float sy=std::sin(rotation[1]),cy=std::cos(rotation[1]);
    const float sz=std::sin(rotation[2]),cz=std::cos(rotation[2]);
    return {{{cy*cz,sx*sy*cz-cx*sz,cx*sy*cz+sx*sz,position[0]},
             {cy*sz,sx*sy*sz+cx*cz,cx*sy*sz-sx*cz,position[1]},
             {-sy,sx*cy,cx*cy,position[2]}}};
}
Matrix Compose(const Matrix& parent,const Matrix& local)
{
    Matrix result{};
    for(int row=0;row<3;++row)for(int col=0;col<4;++col)
    {
        result[row][col]=col==3?parent[row][3]:0;
        for(int k=0;k<3;++k)result[row][col]+=parent[row][k]*local[k][col];
        if(!std::isfinite(result[row][col]))throw std::invalid_argument("Pose BMD fuera de rango.");
    }
    return result;
}
void ApplyPose(WorldModelPreview& result,const std::vector<std::array<int,3>>& bones,const std::vector<Matrix>& pose)
{
    for(std::size_t face=0;face<result.Geometry.size();++face)for(int corner=0;corner<3;++corner)
    {
        auto& point=result.Geometry[face][corner];const auto local=point;
        const auto& matrix=pose.at(static_cast<std::size_t>(bones[face][corner]));
        for(int row=0;row<3;++row)
        {
            point[row]=matrix[row][3];
            for(int col=0;col<3;++col)point[row]+=matrix[row][col]*local[col];
            if(!std::isfinite(point[row]))throw std::invalid_argument("Geometria BMD fuera de rango.");
        }
    }
}
std::vector<Matrix> ReadBones(WorldModelReader& reader, const std::vector<int>& keys, const std::vector<bool>& usedBones)
{
    std::vector<bool> dummy(usedBones.size());
    std::vector<Matrix> pose(usedBones.size());
    for (std::size_t bone = 0; bone < usedBones.size(); ++bone)
    {
        dummy[bone] = reader.Boolean();
        if (dummy[bone])
        {
            if (usedBones[bone]) throw std::invalid_argument("La geometria usa un hueso dummy.");
            continue;
        }
        reader.Name();
        const int parent = reader.SignedShort();
        if (parent < -1 || parent >= static_cast<int>(bone) || (parent >= 0 && dummy[static_cast<std::size_t>(parent)]))
            throw std::invalid_argument("Jerarquia BMD no compatible con la previsualizacion.");
        if(usedBones[bone] && (keys.empty() || keys.front()==0))
            throw std::invalid_argument("El modelo no tiene una pose inicial valida.");
        Point position{},rotation{};
        for (std::size_t action=0;action<keys.size();++action)
        {
            for (int index=0;index<keys[action];++index)
            {const auto value=ReadPoint(reader);if(action==0&&index==0)position=value;}
            for (int index=0;index<keys[action];++index)
            {const auto value=ReadPoint(reader);if(action==0&&index==0)rotation=value;}
        }
        const auto local=LocalPose(position,rotation);
        pose[bone]=parent<0?local:Compose(pose[static_cast<std::size_t>(parent)],local);
    }
    return pose;
}
}
WorldModelPreview WorldModelPreview::Decode(std::span<const std::uint8_t> bytes)
{
    constexpr std::size_t MinimumBytes = 8;
    if (bytes.size() < MinimumBytes || bytes.size() > MaximumFileBytes || bytes[0] != 'B' || bytes[1] != 'M' || bytes[2] != 'D')
        throw std::invalid_argument("Cabecera o longitud BMD no valida.");
    WorldModelPreview result; result.Version = bytes[3];
    std::vector<std::uint8_t> decrypted;
    auto payload = bytes.subspan(4);
    if (result.Version == 12)
    {
        WorldModelReader sizeReader(payload);
        const auto count = sizeReader.Unsigned(4);
        if (count != sizeReader.Remaining()) throw std::invalid_argument("Longitud cifrada BMD inconsistente.");
        decrypted = WorldObjectCodec::Decrypt(bytes.subspan(8)); payload = decrypted;
    }
    else if (result.Version != 10) throw std::invalid_argument("La biblioteca admite BMD 10/12; esta version no esta soportada.");
    WorldModelReader reader(payload);
    result.Name = reader.Name();
    result.MeshCount = reader.Count(MaximumMeshes);
    result.BoneCount = reader.Count(MaximumBones);
    result.ActionCount = reader.Count(MaximumActions);
    std::vector<bool> usedBones(static_cast<std::size_t>(result.BoneCount));
    std::vector<std::array<int,3>> geometryBones;
    ReadMeshes(reader, result, usedBones, geometryBones);
    const auto keys = ReadActions(reader, result.ActionCount);
    const auto pose=ReadBones(reader, keys, usedBones);
    ApplyPose(result,geometryBones,pose);
    if (reader.Remaining() != 0) throw std::invalid_argument("El BMD contiene datos sin interpretar; no se aprobo.");
    return result;
}
}
