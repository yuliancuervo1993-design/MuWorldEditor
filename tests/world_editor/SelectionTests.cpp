#include "WorldSelectionContext.h"
#include "WorldObjectCodec.h"
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

WorldObjectRecord Record(std::int16_t type, float x, float y, float z = 30.0f)
{
    return {type, {x, y, z}, {0.0f, 0.0f, 0.0f}, 1.0f};
}

WorldObjectDocument Document()
{
    WorldObjectFile file;
    file.Records.push_back(Record(1, 100.0f, 200.0f));
    file.Records.push_back(Record(2, 300.0f, 400.0f));
    file.Records.push_back(Record(3, 500.0f, 600.0f));
    WorldObjectDocument document;
    document.Load(std::move(file));
    return document;
}

std::vector<std::uint8_t> Bytes(const WorldObjectDocument& document)
{
    return WorldObjectCodec::Encode(document.Export());
}

void SelectionContract()
{
    auto document = Document();
    WorldSelectionContext selection;
    Check(selection.Empty() && selection.Active() == NoObject, "selection starts empty");

    selection.SelectOnly(1);
    selection.Add(2);
    selection.Add(2);
    Check(selection.Count() == 2 && selection.Contains(1) && selection.Contains(2)
        && selection.Active() == 2, "selection deduplicates and tracks active object");

    selection.Toggle(1);
    Check(selection.Count() == 1 && !selection.Contains(1) && selection.Active() == 2,
        "toggle removes secondary selection");
    selection.Toggle(2);
    Check(selection.Empty() && selection.Active() == NoObject, "toggle active object can empty selection");

    selection.Add(1);
    selection.Add(2);
    document.Remove(2);
    selection.Prune(document);
    Check(selection.Count() == 1 && selection.Active() == 1, "prune removes deleted objects");

    auto legacy = Record(4, 700.0f, 800.0f);
    legacy.BaselineOnly = true;
    WorldObjectFile file;
    file.Records.push_back(legacy);
    WorldObjectDocument legacyDocument;
    legacyDocument.Load(std::move(file));
    selection.SelectOnly(1);
    selection.Prune(legacyDocument);
    Check(selection.Empty(), "prune rejects baseline-only legacy records");
}

void AtomicBatchContract()
{
    auto document = Document();
    const auto baseline = Bytes(document);
    auto first = *document.Find(1);
    auto second = *document.Find(2);
    first.Position[0] += 50.0f;
    second.Position[1] += 75.0f;
    second.Angle[2] = 45.0f;

    Check(document.SetMany({{1, first}, {2, second}}, "Mover seleccion"), "batch transform commits");
    Check(*document.Find(1) == first && *document.Find(2) == second, "batch transform applies every object");
    Check(document.CanUndo() && document.Undo() && !document.CanUndo(), "batch transform is one undo action");
    Check(Bytes(document) == baseline, "single undo restores whole batch byte-exact");
    Check(document.Redo() && *document.Find(1) == first && *document.Find(2) == second,
        "single redo reapplies whole batch");

    Check(document.Undo(), "batch can return to baseline before branch");
    auto third = *document.Find(3);
    third.Position[2] += 10.0f;
    Check(document.SetMany({{3, third}}, "Nueva rama"), "new batch after undo commits");
    Check(!document.CanRedo(), "new batch invalidates previous redo branch");
}

void GroupLifecycleContract()
{
    auto document = Document();
    const auto baseline = Bytes(document);
    auto first = *document.Find(1);
    auto second = *document.Find(2);
    first.Position[0] += 50.0f;
    second.Position[0] += 50.0f;

    const auto ids = document.AddMany({first, second}, "Duplicar seleccion");
    Check(ids.size() == 2 && ids[0] == 4 && ids[1] == 5, "batch add returns stable new ids");
    Check(document.Find(ids[0]) && document.Find(ids[1]), "batch add creates all objects");
    Check(document.Undo(), "batch add is one undo action");
    Check(!document.Find(ids[0]) && !document.Find(ids[1]), "undo batch add removes whole group");
    Check(document.Redo(), "batch add redo restores whole group");

    Check(document.RemoveMany(ids, "Retirar seleccion"), "batch remove commits");
    Check(!document.Find(ids[0]) && !document.Find(ids[1]), "batch remove retires whole group");
    Check(document.Undo(), "batch remove is one undo action");
    Check(document.Find(ids[0]) && document.Find(ids[1]), "undo batch remove restores whole group");

    Check(document.Undo(), "undo returns through batch add");
    Check(Bytes(document) == baseline, "group lifecycle returns byte-exact to baseline");
}

void BatchValidationContract()
{
    auto document = Document();
    const auto before = Bytes(document);
    const auto revision = document.Revision();

    auto valid = *document.Find(1);
    valid.Position[0] += 10.0f;
    auto invalid = *document.Find(2);
    invalid.Scale = MaximumScale + 1.0f;
    Reject<std::invalid_argument>([&] { document.SetMany({{1, valid}, {2, invalid}}, "Invalid"); },
        "invalid second object rejects complete batch");
    Check(Bytes(document) == before && document.Revision() == revision && !document.CanUndo(),
        "failed batch leaves document and history untouched");

    auto duplicate = *document.Find(1);
    duplicate.Position[1] += 10.0f;
    Reject<std::invalid_argument>([&] { document.SetMany({{1, valid}, {1, duplicate}}, "Duplicate"); },
        "duplicate ids rejected");
    Check(Bytes(document) == before && !document.CanUndo(), "duplicate rejection is atomic");

    auto wrongType = *document.Find(1);
    wrongType.Type = 9;
    Reject<std::invalid_argument>([&] { document.SetMany({{1, wrongType}}, "Wrong model"); },
        "batch cannot change object model");
    Check(Bytes(document) == before, "type rejection leaves bytes unchanged");

    const auto same = *document.Find(1);
    Check(!document.SetMany({{1, same}}, "No-op") && !document.CanUndo() && !document.Dirty(),
        "no-op batch creates no history entry");
}
}

int main()
{
    try
    {
        SelectionContract();
        AtomicBatchContract();
        GroupLifecycleContract();
        BatchValidationContract();
        std::cout << "SELECTION_CONTEXT_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
