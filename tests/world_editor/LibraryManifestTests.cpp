#include "WorldLibraryManifest.h"
#include <iostream>
#include <stdexcept>
using namespace Silver::WorldEditor;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void Reject(F test){bool rejected=false;try{test();}catch(const std::exception&){rejected=true;}Check(rejected,"invalid manifest accepted");}
}
int main()
{
    try
    {
        TerrainMappingFile mapping;mapping.Map=1;mapping.Layer1[10]=29;mapping.Layer2[30]=28;
        Check(WorldLibraryManifest::FreeTerrainSlot(mapping,{})==27,"used base and invisible blend slots protected");
        WorldLibraryManifest manifest;const std::string hash(64,'a');
        manifest.Models.emplace(30000,WorldLibraryManifest::Binding{"Item/Sword01.bmd",hash});
        manifest.Terrain.emplace(27,WorldLibraryManifest::Binding{"World3/TileGrass01.OZJ",hash});
        Check(WorldLibraryManifest::FreeTerrainSlot(mapping,manifest.Terrain)==26,"imported slot not reused");
        WorldObjectRecord object;object.Type=30000;object.Position={100,200,30};object.Scale=2;manifest.Objects.push_back(object);
        mapping.Layer1[55]=27;manifest.MappingBytes=TerrainMappingCodec::Encode(mapping);manifest.MappingBaseline=hash;
        const auto bytes=manifest.Encode(0,hash);const auto restored=WorldLibraryManifest::Decode(bytes,0,hash);
        Check(restored.Objects==manifest.Objects,"decorations round trip including transforms");
        Check(restored.Models.at(30000).Path=="Item/Sword01.bmd","exact item resource preserved");
        Check(TerrainMappingCodec::Decode(restored.MappingBytes).Layer1[55]==27,"painted material binding survives reopen");
        Reject([&]{WorldLibraryManifest::Decode(bytes,2,hash);});
        Reject([&]{WorldLibraryManifest::Decode(bytes,0,std::string(64,'b'));});
        manifest.Models.at(30000).Path="../secrets.bmd";Reject([&]{WorldLibraryManifest::Decode(manifest.Encode(0,hash),0,hash);});
        manifest.Models.at(30000).Path="Item/Sword01.bmd";manifest.Objects[0].Type=32000;
        Reject([&]{WorldLibraryManifest::Decode(manifest.Encode(0,hash),0,hash);});
        for(int slot=14;slot<=29;++slot)mapping.Layer1[static_cast<std::size_t>(slot)]=static_cast<std::uint8_t>(slot);
        Check(WorldLibraryManifest::FreeTerrainSlot(mapping,{})==-1,"full palette never overwrites a material");
        std::cout<<"Library manifest: resource identity, round trip, mappings and rejection checks passed\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
