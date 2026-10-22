#include "stdafx.h"
#ifdef _EDITOR
#include "MuPlacementPreview.h"
#include "Render/Models/ZzzBMD.h"
#include "Render/Sprites/GlobalBitmap.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t MaximumPreviewVertices = 196608;
constexpr std::uint32_t PreviewTint = 0xA6D8FFE0u;
void Require(bool condition)
{
    if (!condition) throw std::runtime_error("Modelo sin geometria/materiales seguros para la vista previa.");
}
void ReferencePose(const BMD& model, float (*pose)[3][4])
{
    Require(model.NumBones > 0 && model.NumBones <= MAX_BONES && model.Bones);
    Require(model.NumActions > 0 && model.Actions && model.Actions[0].NumAnimationKeys > 0);
    for (int i = 0; i < model.NumBones; ++i)
    {
        const auto& bone = model.Bones[i];
        if (bone.Dummy) { pose[i][0][0] = pose[i][1][1] = pose[i][2][2] = 1.0f; continue; }
        Require(bone.Parent >= -1 && bone.Parent < i && bone.BoneMatrixes);
        const auto& frame = bone.BoneMatrixes[0];
        Require(frame.Quaternion && frame.Position && frame.Rotation);
        float matrix[3][4]{};
        vec4_t quaternion;
        // Same first action/key as a newly placed static object, with a neutral head.
        AngleQuaternion(frame.Rotation[0], quaternion);
        QuaternionMatrix(quaternion, matrix);
        for (int axis = 0; axis < 3; ++axis) matrix[axis][3] = frame.Position[0][axis];
        if (bone.Parent < 0) std::copy_n(&matrix[0][0], 12, &pose[i][0][0]);
        else { Require(!model.Bones[bone.Parent].Dummy); R_ConcatTransforms(pose[bone.Parent], matrix, pose[i]); }
    }
}
mu::Vertex3D ReadVertex(const BMD& model, const Mesh_t& mesh, const Triangle_t& face,
    int corner, const float (*pose)[3][4])
{
    const int v = face.VertexIndex[corner], n = face.NormalIndex[corner], t = face.TexCoordIndex[corner];
    Require(v >= 0 && v < mesh.NumVertices && n >= 0 && n < mesh.NumNormals && t >= 0 && t < mesh.NumTexCoords);
    const auto& vertex = mesh.Vertices[v]; const auto& normal = mesh.Normals[n];
    Require(vertex.Node >= 0 && vertex.Node < model.NumBones && normal.Node >= 0 && normal.Node < model.NumBones);
    Require(!model.Bones[vertex.Node].Dummy && !model.Bones[normal.Node].Dummy);
    vec3_t position{}, direction{};
    VectorTransform(vertex.Position, pose[vertex.Node], position);
    VectorRotate(normal.Normal, pose[normal.Node], direction);
    const auto& uv = mesh.TexCoords[t];
    for (int axis = 0; axis < 3; ++axis) Require(std::isfinite(position[axis]) && std::isfinite(direction[axis]));
    Require(std::isfinite(uv.TexCoordU) && std::isfinite(uv.TexCoordV));
    return {position[0], position[1], position[2], direction[0], direction[1], direction[2], uv.TexCoordU, uv.TexCoordV, PreviewTint};
}
}
void MuPlacementPreview::Clear()
{
    type_ = -1; ready_ = false; meshes_.clear(); transformed_.reset();
}
bool MuPlacementPreview::TexturesReady() const
{
    const auto& renderer = mu::GetRenderer();
    return ready_ && std::all_of(meshes_.begin(), meshes_.end(), [&renderer](const Mesh& mesh) {
        return renderer.IsTextureRegistered(mesh.Texture);
    });
}
bool MuPlacementPreview::Prepare(std::int16_t type)
{
    if (type == type_) return TexturesReady();
    Clear(); type_ = type;
    try { Build(type); ready_ = !meshes_.empty(); }
    catch (const std::exception&) { meshes_.clear(); ready_ = false; }
    return TexturesReady();
}
void MuPlacementPreview::Build(std::int16_t type)
{
    Require(type >= MODEL_WORLD_OBJECT && type < MAX_WORLD_OBJECTS);
    BuildModel(Models[type]);
}
void MuPlacementPreview::BuildModel(const BMD& model)
{
    Require(model.NumMeshs > 0 && model.NumMeshs <= MAX_MESH && model.Meshs && model.IndexTexture);
    float pose[MAX_BONES][3][4]{};
    ReferencePose(model, pose);
    std::size_t count = 0;
    for (int i = 0; i < model.NumMeshs; ++i)
    {
        const auto& source = model.Meshs[i];
        if (source.NumTriangles == 0) continue;
        Require(source.Texture >= 0 && source.Texture < model.NumMeshs);
        const auto texture = model.IndexTexture[source.Texture];
        if (texture == BITMAP_HIDE) continue;
        Require(texture != BITMAP_UNKNOWN && mu::GetRenderer().IsTextureRegistered(texture));
        Require(source.NumTriangles >= 0 && source.Triangles && source.Vertices && source.Normals && source.TexCoords);
        count += static_cast<std::size_t>(source.NumTriangles) * 3;
        Require(count <= MaximumPreviewVertices);
        Mesh mesh; mesh.Texture = texture; mesh.Local.reserve(static_cast<std::size_t>(source.NumTriangles) * 3);
        for (int f = 0; f < source.NumTriangles; ++f)
        {
            const auto& face = source.Triangles[f]; Require(face.Polygon == 3);
            for (int c = 0; c < 3; ++c) mesh.Local.push_back(ReadVertex(model, source, face, c, pose));
        }
        mesh.Transformed.resize(mesh.Local.size()); mesh.World.resize(mesh.Local.size()); meshes_.push_back(std::move(mesh));
    }
    Require(count > 0);
}
void MuPlacementPreview::Transform(const WorldObjectRecord& record)
{
    auto key = record; key.Position = {};
    if (transformed_ == key) return;
    ValidateRecord(key);
    auto angle = key.Angle; float matrix[3][4]{}; AngleMatrix(angle.data(), matrix);
    minimum_ = std::numeric_limits<float>::max();
    for (auto& mesh : meshes_) for (std::size_t i = 0; i < mesh.Local.size(); ++i)
    {
        const auto& v = mesh.Local[i]; auto& out = mesh.Transformed[i]; out = v;
        const float p[3]{v.x, v.y, v.z}, n[3]{v.nx, v.ny, v.nz}; float r[3]{}, normal[3]{};
        VectorRotate(p, matrix, r); VectorRotate(n, matrix, normal);
        out.x = r[0] * key.Scale; out.y = r[1] * key.Scale; out.z = r[2] * key.Scale;
        out.nx = normal[0]; out.ny = normal[1]; out.nz = normal[2];
        Require(std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z));
        minimum_ = std::min(minimum_, out.z);
    }
    Require(std::isfinite(minimum_)); transformed_ = key;
}
bool MuPlacementPreview::PrepareThumbnail(std::int16_t type, std::vector<PreviewMeshView>& meshes, std::array<float, 3>& low, std::array<float, 3>& high)
{
    meshes.clear();
    if (!Prepare(type)) return false;
    low.fill(std::numeric_limits<float>::max()); high.fill(std::numeric_limits<float>::lowest());
    for (const auto& mesh : meshes_)
    {
        meshes.push_back({mesh.Texture, mesh.Local});
        for (const auto& vertex : mesh.Local)
        {
            const float values[3]{vertex.x, vertex.y, vertex.z};
            for (int axis = 0; axis < 3; ++axis) { low[axis] = std::min(low[axis], values[axis]); high[axis] = std::max(high[axis], values[axis]); }
        }
    }
    return !meshes.empty() && std::all_of(low.begin(), low.end(), [](float value) { return std::isfinite(value); })
        && std::all_of(high.begin(), high.end(), [](float value) { return std::isfinite(value); });
}
bool MuPlacementPreview::Bind(std::int16_t type, const BMD& model)
{
    Clear(); type_ = type;
    try { BuildModel(model); ready_ = !meshes_.empty(); }
    catch (const std::exception&) { Clear(); return false; }
    return TexturesReady();
}
bool MuPlacementPreview::Box(const WorldObjectRecord& record, std::array<std::array<float,3>,8>& corners)
{
    if (!Prepare(record.Type)) return false;
    Transform(record);
    std::array<float,3> low, high; low.fill(std::numeric_limits<float>::max()); high.fill(std::numeric_limits<float>::lowest());
    for (const auto& mesh : meshes_) for (const auto& vertex : mesh.Transformed)
    {
        const float v[]{vertex.x,vertex.y,vertex.z};
        for (int axis=0;axis<3;++axis) { low[axis]=std::min(low[axis],v[axis]); high[axis]=std::max(high[axis],v[axis]); }
    }
    for (int corner=0;corner<8;++corner) for (int axis=0;axis<3;++axis)
        corners[corner][axis]=((corner&(1<<axis))?high[axis]:low[axis])+record.Position[axis];
    return true;
}
bool MuPlacementPreview::MinimumZ(const WorldObjectRecord& record, float& minimum)
{
    if (!Prepare(record.Type)) return false;
    Transform(record); minimum = minimum_; return true;
}
bool MuPlacementPreview::Draw(const WorldObjectRecord& record, bool solid)
{
    if (!Prepare(record.Type)) return false;
    Transform(record); bool submitted = true;
    for (auto& mesh : meshes_)
    {
        for (std::size_t i = 0; i < mesh.Transformed.size(); ++i)
        {
            auto v = mesh.Transformed[i]; v.x += record.Position[0]; v.y += record.Position[1]; v.z += record.Position[2];
            if (solid) v.color = 0xFFFFFFFFu;
            mesh.World[i] = v;
        }
        submitted = mu::GetRenderer().RenderEditorPreview(mesh.World, mesh.Texture) && submitted;
    }
    return submitted;
}
}
#endif
