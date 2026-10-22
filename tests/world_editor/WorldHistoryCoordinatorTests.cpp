#include "WorldHistoryCoordinator.h"
#include <cstdlib>
#include <iostream>

using namespace Silver::WorldEditor;

namespace
{
void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    WorldHistoryCoordinator history;
    Check(!history.CanUndo() && !history.CanRedo(), "fresh history is empty");

    history.Record(WorldHistoryDomain::Objects, "Move 3 Objects");
    history.Record(WorldHistoryDomain::Terrain, "Terrain Stroke");
    history.Record(WorldHistoryDomain::Layers, "Assign Layer");
    history.Record(WorldHistoryDomain::Effects, "Add Point Light");

    Check(history.Entries().size() == 4 && history.Cursor() == 4, "four domains share one timeline");
    Check(history.UndoEntry() && history.UndoEntry()->Domains.front() == WorldHistoryDomain::Effects, "latest effect is first undo");
    Check(history.CommitUndo(), "effect undo commits");
    Check(history.UndoEntry() && history.UndoEntry()->Domains.front() == WorldHistoryDomain::Layers, "layer follows effect");
    Check(history.CommitUndo(), "layer undo commits");
    Check(history.UndoEntry() && history.UndoEntry()->Domains.front() == WorldHistoryDomain::Terrain, "terrain follows layer");
    Check(history.RedoEntry() && history.RedoEntry()->Domains.front() == WorldHistoryDomain::Layers, "redo is chronological");

    history.Record(WorldHistoryDomain::Objects, "Delete Objects");
    Check(!history.CanRedo(), "new edit truncates global redo branch");
    Check(history.Entries().size() == 3, "truncated branch replaced by new edit");
    Check(history.UndoEntry() && history.UndoEntry()->Label == "Delete Objects", "new edit is timeline head");

    history.RecordBatch({WorldHistoryDomain::Objects, WorldHistoryDomain::Layers}, "Duplicate selection");
    Check(history.UndoEntry() && history.UndoEntry()->Domains.size() == 2, "cross-domain edit is one transaction");
    Check(history.UndoEntry()->Domains[0] == WorldHistoryDomain::Objects
        && history.UndoEntry()->Domains[1] == WorldHistoryDomain::Layers, "batch preserves mutation order");

    history.Reset();
    Check(history.Entries().empty() && history.Cursor() == 0, "reset clears map timeline");
    std::cout << "WorldHistoryCoordinatorTests OK\n";
}
