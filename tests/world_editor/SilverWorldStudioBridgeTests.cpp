#include "SilverWorldStudioBridge.h"
#include "WorldObjectCandidate.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Contract root and Data root required");
        const std::filesystem::path contracts = argv[1];
        const std::filesystem::path data = argv[2];
        const auto check = [&](int mapNumber, int mapWorld)
        {
            const auto contract = SilverWorldStudioBridge::TryLoadForMap(contracts, mapNumber, mapWorld);
            if (!contract) throw std::runtime_error("Expected Silver World Studio contract was not found");
            const auto world = data / ("World" + std::to_string(mapWorld));
            const auto object = world / ("EncTerrain" + std::to_string(mapWorld) + ".obj");
            const auto baseline = WorldObjectCodec::Decode(WorldObjectCandidate::Read(object));
            const auto applied = SilverWorldStudioBridge::Apply(*contract, baseline, mapNumber, mapWorld);
            const auto diff=SilverWorldStudioBridge::Diff(*contract,baseline,mapNumber,mapWorld);
            if(applied.Records!=baseline.Records&&diff.empty())throw std::runtime_error("Bridge changes missing from diff");
            for(const auto& change:diff)
            {
                if(change.Index<0||static_cast<std::size_t>(change.Index)>=baseline.Records.size())throw std::runtime_error("Bridge diff index invalid");
                if(change.Before.Type!=change.After.Type||change.Before.BaselineOnly!=change.After.BaselineOnly)throw std::runtime_error("Bridge diff changed OBJ identity");
            }
            const auto candidate = SilverWorldStudioBridge::EncodeCandidate(*contract, baseline, mapNumber, mapWorld);
            const auto restored = SilverWorldStudioBridge::DecodeCandidate(candidate, baseline, mapNumber, mapWorld);
            if (restored.Records != applied.Records)
                throw std::runtime_error("Bridge candidate round-trip changed proposed OBJ");
            std::cout << "BRIDGE_OK map=" << mapNumber << " world=" << mapWorld
                      << " placements=" << contract->Placements.size()
                      << " resolved_models=" << contract->ResolvedModelCount << '\n';
        };
        check(0, 1);
        check(10, 11);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BRIDGE_FAIL " << error.what() << '\n';
        return 1;
    }
}
