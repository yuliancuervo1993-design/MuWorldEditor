#include "PlacementTestSupport.h"
#include <type_traits>
using namespace PlacementTests;
static_assert(!std::is_copy_constructible_v<WorldPlacementSession>);
namespace
{
void NoMutationTests()
{
    Case("select hover rotate scale cancel never edit the document", [] {
        Rig r; const auto before = Capture(r.Document);
        for (int i = 0; i < 1000; ++i) r.Target(static_cast<float>(i), 200);
        r.Placement.RotateSteps(1); r.Placement.SetScalePercent(200); r.Target();
        Check(Capture(r.Document) == before && !r.Document.HasPreview());
        r.Placement.Cancel(); Check(!r.Placement.Active() && Capture(r.Document) == before);
    });
    Case("cancel preserves an existing redo stack", [] {
        Rig r; r.Document.Add(r.Original); r.Document.Undo();
        r.Placement.Select(r.Original, r.Document, Scene); const auto before = Capture(r.Document);
        r.Prime(); r.Placement.Cancel(); Check(Capture(r.Document) == before && r.Document.CanRedo());
    });
    Case("inactive session never places or dereferences a document", [] {
        Rig r; r.Placement.Cancel(); const auto before = Capture(r.Document);
        r.Prime(); Check(r.Button(true) == NoObject && Capture(r.Document) == before);
        Reject<std::logic_error>([&] { r.Placement.RotateSteps(1); });
    });
}
void ConfirmationTests()
{
    Case("palette click must release before a placement click", [] {
        Rig r; const auto before = Capture(r.Document); r.Target();
        Check(r.Button(true) == NoObject && Capture(r.Document) == before);
        r.Button(false); const auto preview = *r.Placement.Preview(); const auto id = r.Button(true);
        Check(id == 2 && *r.Document.Find(id) == preview && !r.Placement.Active());
        Check(r.Button(true) == NoObject && r.Document.Entries().size() == 2);
    });
    Case("confirmed object has exactly one reversible history entry", [] {
        Rig r; const auto before = Capture(r.Document).Bytes; r.Prime(); r.Button(true);
        const auto after = Capture(r.Document).Bytes;
        Check(r.Document.Undo() && !r.Document.CanUndo() && !r.Document.Dirty());
        Check(Capture(r.Document).Bytes == before && r.Document.Redo());
        Check(Capture(r.Document).Bytes == after && !r.Document.CanRedo());
    });
    Case("repeat needs separate presses; repeated frame dispatch never duplicates", [] {
        Rig r; r.Placement.SetRepeat(true); r.Prime(); Check(r.Button(true) == 2);
        for (int i = 0; i < 1000; ++i) { r.Target(); Check(r.Button(true) == NoObject); }
        Check(r.Document.Entries().size() == 2); r.Button(false); r.Target();
        Check(r.Button(true) == 3 && r.Placement.Active());
        Check(r.Document.Undo() && r.Document.Undo() && !r.Document.CanUndo());
    });
    Case("failed press cannot become placement by dragging onto valid ground", [] {
        Rig r; r.Button(false); r.Target(-1, 100); Check(r.Button(true) == NoObject);
        r.Target(); Check(r.Button(true) == NoObject); r.Button(false); Check(r.Button(true) == 2);
    });
}
void SuspensionTests()
{
    Case("panel suspension hides the preview and consumes its click", [] {
        Rig r; r.Prime(); const auto before = Capture(r.Document); r.Placement.Suspend();
        Check(!r.Placement.Preview() && r.Placement.Active());
        Check(r.Placement.CurrentStatus() == Status::Suspended); r.Target();
        Check(r.Button(true) == NoObject && Capture(r.Document) == before);
        r.Button(false); Check(r.Button(true) == 2);
    });
    Case("cancel and choose another prototype still require button release", [] {
        Rig r; r.Prime(); r.Placement.Cancel(); r.Placement.Select(r.Original, r.Document, Scene);
        r.Target(); Check(r.Button(true) == NoObject); r.Button(false); Check(r.Button(true) == 2);
    });
    Case("selection is copied and does not alias an existing object", [] {
        Rig r; auto prototype = r.Original; r.Placement.Select(prototype, r.Document, Scene);
        prototype.Scale = 20; r.Placement.RotateSteps(1); r.Placement.SetScalePercent(50);
        Check(*r.Document.Find(1) == r.Original && Near(r.Placement.Transform().Scale, 1));
    });
}
void ScaleTests()
{
    for (const float percent : {25.0f, 50.0f, 100.0f, 400.0f})
        Case("prototype-relative scale " + std::to_string(percent), [percent] {
            Rig r; r.Placement.SetScalePercent(percent); r.Prime();
            Check(Near(r.Placement.ScalePercent(), percent));
            const auto preview = *r.Placement.Preview(); r.Button(true);
            Check(Near(preview.Scale, 2 * percent / 100) && *r.Document.Find(2) == preview);
        });
    for (const float bad : {-1.0f, 0.0f, 24.0f, 401.0f, std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        Case("reject scale atomically " + std::to_string(bad), [bad] {
            Rig r; r.Prime(); const auto preview = r.Placement.Preview(); const auto before = Capture(r.Document);
            Reject<std::invalid_argument>([&] { r.Placement.SetScalePercent(bad); });
            Check(r.Placement.Preview() == preview && Capture(r.Document) == before);
        });
    Case("minimum absolute scale intersects percent range", [] {
        Rig r; r.Original.Scale = MinimumScale; r.Placement.Select(r.Original, r.Document, Scene);
        Check(r.Placement.ScalePercentRange() == std::pair{100.0f, 400.0f});
        Reject<std::invalid_argument>([&] { r.Placement.SetScalePercent(25); });
        r.Placement.SetScalePercent(400); Check(Near(r.Placement.Transform().Scale, 0.04f));
        r.Placement.SetScalePercent(100); Check(r.Placement.Transform().Scale == MinimumScale);
    });
    Case("maximum absolute scale intersects percent range", [] {
        Rig r; r.Original.Scale = MaximumScale; r.Placement.Select(r.Original, r.Document, Scene);
        Check(r.Placement.ScalePercentRange() == std::pair{25.0f, 100.0f});
        Reject<std::invalid_argument>([&] { r.Placement.SetScalePercent(400); });
        r.Placement.SetScalePercent(25); Check(r.Placement.Transform().Scale == 25);
    });
}
void RotationTests()
{
    for (const int steps : {-1, 0, 1, 24, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
        Case("rotation steps " + std::to_string(steps), [steps] {
            Rig r; r.Placement.RotateSteps(steps); const auto value = r.Placement.Transform();
            Check(value.Angle[0] == 7 && value.Angle[1] == 13 && value.Angle[2] >= 0 && value.Angle[2] < 360);
            double expected = std::fmod(345.0 + static_cast<double>(steps) * 15.0, 360.0);
            if (expected < 0) expected += 360;
            Check(Near(value.Angle[2], static_cast<float>(expected)));
            r.Prime(); const auto preview = *r.Placement.Preview(); r.Button(true);
            Check(*r.Document.Find(2) == preview && r.Document.Undo() && r.Document.Redo());
            Check(*r.Document.Find(2) == preview);
        });
    Case("reset preserves the original inclined rotation", [] {
        Rig r; r.Placement.RotateSteps(8); r.Placement.ResetRotation();
        Check(r.Placement.Transform().Angle == r.Original.Angle);
    });
}
void SupportTests()
{
    Case("grid is applied before sampling height on a slope", [] {
        Rig r; r.Ground.Slope = true; r.Target();
        Check(r.Ground.SampledX == 150 && r.Ground.SampledY == 200);
        Check(Near(r.Placement.Preview()->Position[2], 1004));
    });
    Case("grid off retains raw coordinates", [] {
        Rig r; r.Placement.SetGrid(false); r.Target();
        Check(r.Ground.SampledX == 126 && r.Ground.SampledY == 176);
    });
    Case("base support uses full rotation scale and origin without translation", [] {
        Rig r; r.Placement.RotateSteps(1); r.Placement.SetScalePercent(200); r.Target();
        Check(r.Ground.SampledTransform == r.Placement.Transform());
        Check(r.Ground.SampledTransform.Position == std::array<float,3>{});
        Check(Near(r.Placement.Preview()->Position[2], 108));
    });
    Case("positive geometry minimum aligns base without a universal offset", [] {
        Rig r; r.Ground.Minimum = 25.0f; r.Target(); Check(r.Placement.Preview()->Position[2] == 75);
    });
    Case("explicit pivot support does not require geometry minimum", [] {
        Rig r; r.Ground.Geometry = false; r.Placement.SetSupport(WorldPlacementSession::Support::Pivot);
        r.Target(); Check(r.Placement.Preview()->Position[2] == 100);
    });
    Case("snap outside the map is rejected rather than silently clamped", [] {
        Rig r; r.Target(MapEdge - 1, 50); Check(!r.Placement.Preview());
        Check(r.Placement.CurrentStatus() == Status::OutsideMap);
        r.Placement.SetGrid(false); r.Target(MapEdge - 1, 50); Check(r.Placement.Preview().has_value());
    });
}
void InvalidTargetTests()
{
    for (const float bad : {-1.0f, MapEdge, std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        for (const bool xAxis : {false, true})
            Case("invalid coordinate " + std::to_string(bad) + (xAxis ? " X" : " Y"), [bad,xAxis] {
                Rig r; r.Prime(); const auto before = Capture(r.Document);
                r.Target(xAxis ? bad : 100, xAxis ? 100 : bad);
                Check(!r.Placement.Preview() && r.Placement.CurrentStatus() == Status::OutsideMap);
                Check(r.Button(true) == NoObject && Capture(r.Document) == before);
            });
    for (const float step : {0.0f, -1.0f, 501.0f, std::numeric_limits<float>::quiet_NaN()})
        Case("invalid grid step " + std::to_string(step), [step] {
            Rig r; r.Prime(); const auto before = r.Placement.Preview();
            Reject<std::invalid_argument>([&] { r.Placement.SetGrid(true, step); }); Check(r.Placement.Preview() == before);
        });
    Case("changing settings removes stale confirmation until target is resampled", [] {
        Rig r; r.Prime(); r.Placement.SetGrid(false); Check(r.Button(true) == NoObject);
        r.Button(false); r.Target(); r.Placement.RotateSteps(1); Check(r.Button(true) == NoObject);
        r.Button(false); r.Target(); r.Placement.SetScalePercent(200); Check(r.Button(true) == NoObject);
    });
}
void ResourceTests()
{
    Case("lost model or texture readiness is rechecked on the actual click", [] {
        Rig r; r.Prime(); const auto before = Capture(r.Document); r.Ground.Ready = false;
        Check(r.Button(true) == NoObject && r.Placement.CurrentStatus() == Status::ModelUnavailable);
        Check(Capture(r.Document) == before); r.Ground.Ready = true; r.Target();
        Check(r.Button(true) == NoObject); r.Button(false); Check(r.Button(true) == 2);
    });
    Case("surface height is rechecked on confirmation", [] {
        Rig r; r.Prime(); r.Ground.Height = 200; Check(r.Button(true) == 2);
        Check(r.Document.Find(2)->Position[2] == 204);
    });
    Case("missing ground blocks placement", [] {
        Rig r; r.Ground.Ground = false; r.Prime();
        Check(r.Placement.CurrentStatus() == Status::SurfaceUnavailable && r.Button(true) == NoObject);
    });
    Case("missing reference geometry blocks base support", [] {
        Rig r; r.Ground.Geometry = false; r.Prime();
        Check(r.Placement.CurrentStatus() == Status::ModelUnavailable && r.Button(true) == NoObject);
    });
    Case("failed query clears the formerly valid preview", [] {
        Rig r; r.Prime(); r.Ground.Throw = true;
        Reject<std::runtime_error>([&] { r.Target(); }); Check(!r.Placement.Preview());
    });
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        Case("nonfinite ground rejected " + std::to_string(bad), [bad] {
            Rig r; r.Ground.Height = bad; r.Prime();
            Check(!r.Placement.Preview() && r.Placement.CurrentStatus() == Status::InvalidSurface);
        });
    Case("nonfinite minimum rejected", [] {
        Rig r; r.Ground.Minimum = std::numeric_limits<float>::quiet_NaN(); r.Prime();
        Check(!r.Placement.Preview() && r.Placement.CurrentStatus() == Status::InvalidSurface);
    });
    Case("finite height offset overflow rejected", [] {
        Rig r; r.Ground.Height = std::numeric_limits<float>::max();
        r.Ground.Minimum = -std::numeric_limits<float>::max(); r.Prime();
        Check(!r.Placement.Preview() && r.Placement.CurrentStatus() == Status::InvalidSurface);
    });
}
void IdentityTests()
{
    Case("scene generation change cancels without touching confirmed edits", [] {
        Rig r; r.Prime(); const auto before = Capture(r.Document);
        Check(r.Placement.PrimaryButton(true, r.Document, Scene + 1, r.Ground) == NoObject);
        Check(!r.Placement.Active() && r.Placement.CurrentStatus() == Status::SceneChanged);
        Check(Capture(r.Document) == before);
    });
    Case("different document with same revision is not interchangeable", [] {
        Rig r, other; r.Prime(); const auto before = Capture(other.Document);
        Check(r.Placement.PrimaryButton(true, other.Document, Scene, r.Ground) == NoObject);
        Check(r.Placement.CurrentStatus() == Status::DocumentChanged && Capture(other.Document) == before);
    });
    Case("document reload invalidates a provisional placement", [] {
        Rig r; r.Prime(); r.Document.Load(r.Document.Export()); const auto before = Capture(r.Document);
        Check(r.Button(true) == NoObject && r.Placement.CurrentStatus() == Status::DocumentChanged);
        Check(Capture(r.Document) == before);
    });
    Case("another active edit is not cancelled or overwritten", [] {
        Rig r; r.Prime(); r.Document.Begin(1, "other edit"); const auto before = Capture(r.Document);
        Check(r.Button(true) == NoObject && Capture(r.Document) == before && r.Document.HasPreview());
    });
    Case("invalid selection preserves the previous provisional state", [] {
        Rig r; r.Prime(); const auto before = r.Placement.Preview(); auto bad = r.Original; bad.Scale = 0;
        Reject<std::invalid_argument>([&] { r.Placement.Select(bad, r.Document, Scene); });
        Check(r.Placement.Preview() == before);
        Reject<std::logic_error>([&] { r.Placement.Select(r.Original, r.Document, 0); });
        Check(r.Placement.Preview() == before);
    });
    Case("uninitialized document rejected", [] {
        Rig r; WorldObjectDocument empty;
        Reject<std::logic_error>([&] { r.Placement.Select(r.Original, empty, Scene); });
    });
    Case("negative-border prototype is not moved or normalized on selection", [] {
        Rig r; r.Original.Position[0] = -0.1f; WorldObjectFile file; file.Records.push_back(r.Original);
        r.Document.Load(file); const auto before = Capture(r.Document);
        r.Placement.Select(r.Original, r.Document, Scene); r.Prime();
        Check(Capture(r.Document) == before); Check(r.Button(true) == 2);
        Check(*r.Document.Find(1) == r.Original && r.Document.Find(2)->Position[0] >= 0);
    });
}
void LimitAndPersistenceTests()
{
    Case("object limit rejects before insertion or history mutation", [] {
        Rig r; WorldObjectFile file; file.Records.assign(MaximumObjects, r.Original); r.Document.Load(file);
        r.Placement.Select(r.Original, r.Document, Scene); const auto before = Capture(r.Document);
        r.Prime(); Check(r.Placement.CurrentStatus() == Status::ObjectLimit);
        Check(r.Button(true) == NoObject && Capture(r.Document) == before);
    });
    for (const auto encoding : {ObjectEncoding::LegacyPlain, ObjectEncoding::PlainWithMap, ObjectEncoding::MapEncrypted})
        Case("confirmed transform round-trip encoding " + std::to_string(static_cast<int>(encoding)), [encoding] {
            Rig r; auto file = r.Document.Export(); file.Encoding = encoding; r.Document.Load(file);
            r.Placement.Select(r.Original, r.Document, Scene); const auto before = Capture(r.Document);
            r.Placement.RotateSteps(-2); r.Placement.SetScalePercent(175); r.Prime();
            Check(Capture(r.Document) == before); const auto preview = *r.Placement.Preview(); r.Button(true);
            const auto bytes = Capture(r.Document).Bytes; const auto decoded = WorldObjectCodec::Decode(bytes);
            Check(decoded.Records.size() == 2 && decoded.Records.back() == preview);
            Check(WorldObjectCodec::Encode(decoded) == bytes);
            Check(r.Document.Undo() && Capture(r.Document).Bytes == before.Bytes);
        });
}
}
// This suite uses synthetic in-memory documents only; it does not open the game.
int main()
{
    NoMutationTests();
    ConfirmationTests();
    SuspensionTests();
    ScaleTests();
    RotationTests();
    SupportTests();
    InvalidTargetTests();
    ResourceTests();
    IdentityTests();
    LimitAndPersistenceTests();
    std::cout << "PLACEMENT CASES passed=" << passed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
