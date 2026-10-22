#include "WorldPlacementSession.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
constexpr double FullTurn = 360.0;
constexpr float WholePercent = 100.0f;
bool InsideMap(float value)
{
    return std::isfinite(value) && value >= 0.0f && value < MapEdge;
}
}
void WorldPlacementSession::Select(const WorldObjectRecord& prototype,
    const WorldObjectDocument& document, SceneId scene)
{
    ValidateRecord(prototype);
    if (!scene || !document.Revision() || document.HasPreview())
        throw std::logic_error("Escena no preparada o transformacion en curso.");
    prototype_ = prototype; transform_ = prototype;
    transform_.Position = {};
    document_ = &document; scene_ = scene; documentRevision_ = document.Revision();
    primaryDown_ = true; // The palette click must be released before placement.
    ClearTarget(Status::WaitingForTarget);
}
void WorldPlacementSession::RequireActive() const
{
    if (!Active()) throw std::logic_error("No hay un modelo por colocar.");
}
const WorldObjectRecord& WorldPlacementSession::Transform() const
{
    RequireActive(); return transform_;
}
void WorldPlacementSession::ClearTarget(Status status)
{
    preview_.reset(); target_.reset(); status_ = status;
}
void WorldPlacementSession::Invalidate(Status status)
{
    prototype_.reset(); document_ = nullptr; scene_ = 0; documentRevision_ = 0;
    primaryDown_ = true; ClearTarget(status);
}
void WorldPlacementSession::Cancel() { Invalidate(Status::Inactive); }
void WorldPlacementSession::Suspend()
{
    if (!Active()) return;
    primaryDown_ = true; ClearTarget(Status::Suspended);
}
bool WorldPlacementSession::CheckContext(const WorldObjectDocument& document, SceneId scene)
{
    if (!Active()) return false;
    if (scene != scene_) { Invalidate(Status::SceneChanged); return false; }
    if (&document != document_ || document.Revision() != documentRevision_ || document.HasPreview())
    { Invalidate(Status::DocumentChanged); return false; }
    return true;
}
void WorldPlacementSession::SetGrid(bool enabled, float step)
{
    if (!std::isfinite(step) || step < MinimumGridStep || step > MaximumGridStep)
        throw std::invalid_argument("Paso de grilla fuera de limites.");
    if (gridEnabled_ == enabled && gridStep_ == step) return;
    gridEnabled_ = enabled; gridStep_ = step;
    if (Active()) ClearTarget(Status::WaitingForTarget);
}
void WorldPlacementSession::SetSupport(Support support)
{
    if (support != Support::ModelBase && support != Support::Pivot)
        throw std::invalid_argument("Modo de apoyo desconocido.");
    if (support_ == support) return;
    support_ = support;
    if (Active()) ClearTarget(Status::WaitingForTarget);
}
void WorldPlacementSession::RotateSteps(int steps)
{
    RequireActive();
    if (!steps) return;
    double yaw = std::fmod(static_cast<double>(transform_.Angle[2]) +
        static_cast<double>(steps) * RotationStep, FullTurn);
    if (yaw < 0) yaw += FullTurn;
    transform_.Angle[2] = static_cast<float>(yaw);
    ClearTarget(Status::WaitingForTarget);
}
void WorldPlacementSession::ResetRotation()
{
    RequireActive(); transform_.Angle = prototype_->Angle;
    ClearTarget(Status::WaitingForTarget);
}
std::pair<float, float> WorldPlacementSession::ScalePercentRange() const
{
    RequireActive();
    const double minimum = std::max(static_cast<double>(MinimumPercent),
        static_cast<double>(MinimumScale) / prototype_->Scale * WholePercent);
    const double maximum = std::min(static_cast<double>(MaximumPercent),
        static_cast<double>(MaximumScale) / prototype_->Scale * WholePercent);
    float low = static_cast<float>(minimum), high = static_cast<float>(maximum);
    // Report representable slider endpoints strictly inside the absolute limits.
    if (low < minimum) low = std::nextafter(low, std::numeric_limits<float>::infinity());
    if (high > maximum) high = std::nextafter(high, -std::numeric_limits<float>::infinity());
    return {low, high};
}
float WorldPlacementSession::ScalePercent() const
{
    RequireActive(); return transform_.Scale / prototype_->Scale * WholePercent;
}
void WorldPlacementSession::SetScalePercent(float percent)
{
    const auto [minimum, maximum] = ScalePercentRange();
    if (!std::isfinite(percent) || percent < minimum || percent > maximum)
        throw std::invalid_argument("Porcentaje fuera del rango seguro del prototipo.");
    auto changed = transform_;
    changed.Scale = static_cast<float>(static_cast<double>(prototype_->Scale) * percent / WholePercent);
    ValidateRecord(changed);
    if (changed.Scale == transform_.Scale) return;
    transform_ = changed; ClearTarget(Status::WaitingForTarget);
}
bool WorldPlacementSession::ResolveSupport(WorldObjectRecord& record, const WorldPlacementSurface& surface)
{
    float height = 0.0f, minimum = 0.0f;
    if (!surface.HeightAt(record.Position[0], record.Position[1], height))
    { status_ = Status::SurfaceUnavailable; return false; }
    if (support_ == Support::ModelBase && !surface.MinimumModelZ(transform_, minimum))
    { status_ = Status::ModelUnavailable; return false; }
    if (!std::isfinite(height) || !std::isfinite(minimum))
    { status_ = Status::InvalidSurface; return false; }
    record.Position[2] = height - minimum;
    if (!std::isfinite(record.Position[2]))
    { status_ = Status::InvalidSurface; return false; }
    return true;
}
bool WorldPlacementSession::ResolveTarget(float x, float y, const WorldPlacementSurface& surface)
{
    if (!InsideMap(x) || !InsideMap(y)) { status_ = Status::OutsideMap; return false; }
    if (!surface.ModelReady(transform_.Type)) { status_ = Status::ModelUnavailable; return false; }
    auto record = transform_;
    record.Position[0] = gridEnabled_ ? static_cast<float>(std::round(static_cast<double>(x) / gridStep_) * gridStep_) : x;
    record.Position[1] = gridEnabled_ ? static_cast<float>(std::round(static_cast<double>(y) / gridStep_) * gridStep_) : y;
    if (!InsideMap(record.Position[0]) || !InsideMap(record.Position[1]))
    { status_ = Status::OutsideMap; return false; }
    if (!ResolveSupport(record, surface)) return false;
    SpatialBlock(record);
    preview_ = record; target_ = std::array{x, y}; status_ = Status::Ready;
    return true;
}
void WorldPlacementSession::UpdateTarget(float x, float y, const WorldObjectDocument& document,
    SceneId scene, const WorldPlacementSurface& surface)
{
    if (!CheckContext(document, scene)) return;
    ClearTarget(Status::WaitingForTarget); // A failed query must never leave a stale valid preview.
    if (document.Entries().size() >= MaximumObjects) { status_ = Status::ObjectLimit; return; }
    ResolveTarget(x, y, surface);
}
ObjectId WorldPlacementSession::PrimaryButton(bool down, WorldObjectDocument& document,
    SceneId scene, const WorldPlacementSurface& surface)
{
    if (!CheckContext(document, scene)) return NoObject;
    const bool pressed = down && !primaryDown_;
    primaryDown_ = down; // Invalid presses are consumed too: dragging into valid ground is not a click.
    if (!pressed || !preview_ || !target_) return NoObject;
    const auto target = *target_;
    UpdateTarget(target[0], target[1], document, scene, surface);
    if (!preview_) return NoObject;
    const auto id = document.Add(*preview_);
    if (repeat_)
    {
        documentRevision_ = document.Revision();
        ClearTarget(Status::WaitingForTarget);
    }
    else Cancel();
    return id;
}
}
