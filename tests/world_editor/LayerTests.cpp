#include "WorldLayerDocument.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Silver::WorldEditor;

namespace
{
int passed = 0;

void Check(bool value, const char* name)
{
    if (!value) throw std::runtime_error(name);
    ++passed;
    std::cout << "PASS " << name << '\n';
}

template<class Error, class Action>
void Reject(Action action, const char* name)
{
    try { action(); }
    catch (const Error&) { Check(true, name); return; }
    throw std::runtime_error(name);
}

WorldObjectRecord Record(std::int16_t type, float x)
{
    return {type, {x, 200.0f, 30.0f}, {0.0f, 0.0f, 0.0f}, 1.0f};
}

WorldObjectDocument Document()
{
    WorldObjectFile file;
    file.Records.push_back(Record(1, 100.0f));
    file.Records.push_back(Record(2, 300.0f));
    file.Records.push_back(Record(3, 500.0f));
    WorldObjectDocument document;
    document.Load(std::move(file));
    return document;
}

void LayerContract()
{
    WorldLayerDocument layers;
    Check(layers.Layers().size() == 1 && layers.Layers().front().Id == DefaultWorldLayer
        && layers.Layers().front().Name == "Base", "layers start with Base");

    const auto decor = layers.Add("Decoracion");
    const auto gameplay = layers.Add("Gameplay");
    Check(decor != gameplay && layers.Layers().size() == 3, "layers add unique ids");
    Reject<std::invalid_argument>([&] { layers.Add("decoracion"); }, "layer names are case-insensitive unique");
    Check(layers.Rename(gameplay, "Spawns"), "layer rename commits");
    Check(layers.SetVisible(decor, false) && !layers.SetVisible(decor, false), "visibility changes only once");
    Check(layers.SetLocked(decor, true) && layers.Find(decor)->Locked, "layer lock changes");
    Check(layers.Dirty(), "layer edits mark document dirty");
    layers.MarkSaved();
    Check(!layers.Dirty(), "mark saved clears layer dirty state");
    layers.SetLocked(decor, false);
    Check(layers.Dirty(), "later layer edit becomes dirty again");
}

void LayerHistoryContract()
{
    WorldLayerDocument layers;
    const auto decor = layers.Add("Decoracion");
    layers.AssignMany(std::vector<ObjectId>{1, 2}, decor);
    layers.SetVisible(decor, false);
    Check(layers.CanUndo() && layers.Dirty(), "layer history tracks edits");

    Check(layers.Undo(), "layer visibility undo");
    Check(layers.Find(decor)->Visible, "undo restores layer visibility");
    Check(layers.Undo(), "layer assignment undo");
    Check(layers.LayerFor(1) == DefaultWorldLayer && layers.LayerFor(2) == DefaultWorldLayer,
        "undo restores group layer assignment atomically");
    Check(layers.Redo(), "layer assignment redo");
    Check(layers.LayerFor(1) == decor && layers.LayerFor(2) == decor,
        "redo restores group layer assignment");

    layers.MarkSaved();
    Check(!layers.Dirty(), "saved layer snapshot is clean");
    layers.SetLocked(decor, true);
    Check(layers.Dirty(), "post-save layer change is dirty");
    Check(layers.Undo() && !layers.Dirty(), "undo to saved layer snapshot clears dirty state");
}

void AssignmentContract()
{
    WorldLayerDocument layers;
    const auto decor = layers.Add("Decoracion");
    Check(layers.Assign(1, decor), "single object assignment");
    Check(layers.LayerFor(1) == decor && !layers.IsLocked(1), "assignment resolves layer properties");

    const std::vector<ObjectId> ids{2, 3};
    Check(layers.AssignMany(ids, decor), "multi assignment commits");
    Check(layers.LayerFor(2) == decor && layers.LayerFor(3) == decor, "multi assignment covers all ids");
    Reject<std::invalid_argument>([&] {
        const std::vector<ObjectId> duplicate{1, 1};
        layers.AssignMany(duplicate, decor);
    }, "multi assignment rejects duplicates");

    Check(layers.Remove(decor), "custom layer removal");
    Check(layers.LayerFor(1) == DefaultWorldLayer && layers.LayerFor(2) == DefaultWorldLayer,
        "removed layer reassigns objects to Base");
    Reject<std::invalid_argument>([&] { layers.Remove(DefaultWorldLayer); }, "Base layer cannot be removed");
}

void PruneContract()
{
    auto document = Document();
    WorldLayerDocument layers;
    const auto decor = layers.Add("Decoracion");
    layers.Assign(1, decor);
    layers.Assign(2, decor);
    const auto before = layers.Revision();

    document.Remove(2);
    layers.Prune(document);
    Check(layers.LayerFor(2) == decor && layers.Revision() == before,
        "temporary remove preserves layer assignment for undo");

    Check(document.Undo(), "removed object can be restored");
    layers.Prune(document);
    Check(layers.LayerFor(1) == decor && layers.LayerFor(2) == decor,
        "undo restores object with its session layer");
}
}

int main()
{
    try
    {
        LayerContract();
        LayerHistoryContract();
        AssignmentContract();
        PruneContract();
        std::cout << "WORLD_LAYER_DOCUMENT_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
