#include "stdafx.h"
#ifdef _EDITOR
#include "MuTerrainTools.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "World/MapInfra/MapManager.h"
#include "imgui.h"
#include <algorithm>
#include <stdexcept>
namespace Silver::WorldEditor
{
void MuTerrainTools::Open()
{
    if(gMapManager.WorldActive!=0)throw std::runtime_error("Pinceles validados por ahora en Lorencia.");
    if(store_&&document_.Dirty())throw std::runtime_error("Guarda el borrador de terreno antes de volver a abrir.");
    auto store=std::make_unique<TerrainCandidateStore>("Data/World1","EditorCandidates/LorenciaTerrain");
    auto doc=store->NewDocument();
    for(int i=0;i<TerrainCells;++i)
        if(static_cast<std::uint16_t>(doc.Attributes()[i]&0xFF)!=TerrainWall[i])
            throw std::runtime_error("Los atributos de la escena no coinciden con el original. No se reemplazo el terreno.");
    document_=std::move(doc);store_=std::move(store);
    appliedHeights_=document_.Heights();appliedAttributes_=document_.Attributes();
    revision_=document_.Revision();attached_=true;
}
void MuTerrainTools::Synchronize(bool force)
{
    if(!attached_||gMapManager.WorldActive!=0||document_.Revision()==revision_)return;
    constexpr double PreviewInterval=0.05;
    const double now=ImGui::GetTime();
    if(!force&&document_.Drawing()&&now-lastSync_<PreviewInterval)return;
    bool heightsChanged=false;
    for(int i=0;i<TerrainCells;++i)
    {
        const auto height=document_.Heights()[i];const auto attr=document_.Attributes()[i];
        if(height!=appliedHeights_[i]){BackTerrainHeight[i]=height*HeightUnit;appliedHeights_[i]=height;heightsChanged=true;}
        const auto changed=static_cast<std::uint16_t>(attr^appliedAttributes_[i]);
        if(changed){TerrainWall[i]=static_cast<WORD>((TerrainWall[i]&~changed)|(attr&changed));appliedAttributes_[i]=attr;}
    }
    if(heightsChanged){CreateTerrainNormal();CreateTerrainLight();}
    revision_=document_.Revision();lastSync_=now;
}
void MuTerrainTools::Draw(IWorldEditorScene& scene)
{
    if(!store_||!attached_)return;
    try
    {
        Synchronize();const bool save=ui_.Draw(document_,scene);Synchronize();
        if(save)Save();
    }
    catch(const std::exception& error){ui_.SetStatus(error.what());}
}
void MuTerrainTools::Save()
{
    if(!store_)return;
    const auto path=store_->Save(document_);document_.MarkSaved();
    ui_.SetStatus("Copia guardada: "+path.generic_string()+". No se aplico al servidor.");
}
void MuTerrainTools::Cancel(){document_.Cancel();Synchronize(true);}
void MuTerrainTools::BeforeMapChange(){Cancel();attached_=false;}
}
#endif
