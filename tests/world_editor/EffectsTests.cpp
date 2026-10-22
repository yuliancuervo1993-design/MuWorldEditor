#include "WorldEffectDocument.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << "\n";
}
}

int main()
{
    const auto root = std::filesystem::current_path() / ".tools" / "effects-tests-runtime";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    try
    {
        WorldEffectDocument document;
        document.Load(root / "missing.json", 0, 1);
        Check(!document.Dirty(), "empty missing sidecar is clean");

        const auto id = document.AddPointLight({12800.0f, 12800.0f, 200.0f});
        Check(document.Dirty(), "adding light marks dirty");
        Check(document.Lights().size() == 1, "one light added");
        Check(document.CanUndo(), "undo available after add");

        auto changed = *document.Find(id);
        changed.Color = {0.2f, 0.4f, 1.0f};
        changed.Intensity = 1.75f;
        changed.Radius = 7;
        Check(document.UpdatePointLight(id, changed), "update light");
        Check(document.Find(id)->Radius == 7, "radius updated");

        document.Undo();
        Check(document.Find(id)->Radius == 3, "undo restores prior light");
        document.Redo();
        Check(document.Find(id)->Radius == 7, "redo reapplies light");

        WorldRuntimeEffect fire;fire.Preset="Fire";fire.Kind=0;fire.Type=100;fire.SubType=0;fire.Position={12900.0f,12800.0f,210.0f};fire.Scale=1.25f;
        const auto effectId=document.AddRuntimeEffect(fire);
        Check(document.RuntimeEffects().size()==1,"runtime effect added");
        Check(document.FindRuntimeEffect(effectId)->Preset=="Fire","runtime effect lookup");
        auto changedEffect=*document.FindRuntimeEffect(effectId);changedEffect.Scale=1.75f;
        Check(document.UpdateRuntimeEffect(effectId,changedEffect),"runtime effect updated");
        Check(document.FindRuntimeEffect(effectId)->Scale==1.75f,"runtime effect scale updated");
        document.Undo();Check(document.FindRuntimeEffect(effectId)->Scale==1.25f,"undo runtime effect update");
        document.Redo();Check(document.FindRuntimeEffect(effectId)->Scale==1.75f,"redo runtime effect update");

        const auto file = root / "candidates" / "Map0-World1" / "effects.json";
        document.Save(file, 0, 1);
        Check(std::filesystem::is_regular_file(file), "effects sidecar saved");
        Check(!document.Dirty(), "save marks clean");

        WorldEffectDocument loaded;
        loaded.Load(file, 0, 1);
        Check(loaded.Lights().size() == 1, "saved light reloads");
        Check(loaded.Lights().front().Color[2] == 1.0f, "saved color reloads");
        Check(loaded.Lights().front().Radius == 7, "saved radius reloads");
        Check(loaded.RuntimeEffects().size()==1,"saved runtime effect reloads");
        Check(loaded.RuntimeEffects().front().Scale==1.75f,"saved runtime effect scale reloads");

        Check(loaded.Remove(loaded.Lights().front().Id), "remove light");
        Check(loaded.Lights().empty(), "light removed");
        loaded.Undo();
        Check(loaded.Lights().size() == 1, "undo remove restores light");

        Check(!std::filesystem::exists(root / "Data"), "effects never create Data");
        std::filesystem::remove_all(root, ignored);
        std::cout << "ALL EFFECT DOCUMENT TESTS PASSED\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << "\n";
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
