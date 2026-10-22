#include "TerrainBrushUI.h"
#include "../core/TerrainHeightFile.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
namespace Silver::WorldEditor
{
namespace
{
constexpr float CellWidth=100.0f;
constexpr float OverlayLift=4.0f;
constexpr int OverlayRange=12;
constexpr ImU32 SafeFill=IM_COL32(50,215,110,65), BlockFill=IM_COL32(240,70,65,70);
constexpr ImU32 BrushLine=IM_COL32(255,220,65,255);
constexpr const char* ToolNames[]={"Subir terreno","Bajar terreno","Nivelar","Suavizar","Zona segura (SafeZone)","Bloquear paso (NoMove)"};
}
bool TerrainBrushUI::Draw(TerrainBrushDocument& document,IWorldEditorScene& scene)
{
    bool save=false;
    ImGui::SetNextWindowPos({16,105},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320,480},ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Terreno y zonas | LAB")) DrawControls(document,save);
    ImGui::End();
    PaintViewport(document,scene);
    return save;
}
void TerrainBrushUI::DrawControls(TerrainBrushDocument& document,bool& save)
{
    ImGui::TextWrapped("Pinta directamente sobre el suelo. Esta vista NO cambia el servidor.");
    ImGui::BeginDisabled(document.Drawing());
    ImGui::Combo("Herramienta",&tool_,ToolNames,6);
    ImGui::SliderInt("Radio (celdas)",&radius_,0,16);
    if (tool_<4) ImGui::SliderInt("Intensidad",&strength_,1,64);
    if (tool_==2)
    {
        ImGui::Checkbox("Tomar altura al iniciar",&sampleLevel_);
        if (!sampleLevel_) ImGui::SliderInt("Altura objetivo",&level_,0,255);
    }
    if (tool_>=4) ImGui::Checkbox("Quitar en vez de agregar",&erase_);
    ImGui::Checkbox("Mostrar zonas cercanas",&showZones_);
    ImGui::Separator();
    ImGui::BeginDisabled(!document.CanUndo());if(ImGui::Button("Deshacer trazo"))document.Undo();ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(!document.CanRedo());if(ImGui::Button("Rehacer"))document.Redo();ImGui::EndDisabled();
    if(ImGui::Button("Guardar terreno y zonas"))save=true;
    ImGui::EndDisabled();
    ImGui::TextWrapped(document.Dirty()?"Cambios sin guardar":"Sin cambios pendientes");
    ImGui::TextWrapped("Verde: zona segura. Rojo: paso bloqueado. Amarillo: pincel. Esc cancela el trazo.");
    ImGui::TextWrapped("Cada pasada afecta una vez a cada celda. Solta y repeti para seguir subiendo o bajando.");
    ImGui::TextWrapped("La celda de control (135,123) conserva sus flags para que MuMain pueda recargar Lorencia.");
    ImGui::TextWrapped("%s",message_.c_str());
}
void TerrainBrushUI::PaintViewport(TerrainBrushDocument& document,IWorldEditorScene& scene)
{
    if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {document.Cancel();waitRelease_=true;}
    if (waitRelease_) {if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))waitRelease_=false;return;}
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !document.Drawing())
    {
        if(ImGui::IsKeyPressed(ImGuiKey_Z,false))document.Undo();
        if(ImGui::IsKeyPressed(ImGuiKey_Y,false))document.Redo();
    }
    if (document.Drawing() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))document.Commit();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::GetIO().WantTextInput)
    {
        if(document.Drawing())document.Paint(-1,-1);
        return;
    }
    const auto mouse=ImGui::GetMousePos();std::array<float,3> world{};
    if(!scene.Placement(mouse.x,mouse.y,world)) {if(document.Drawing())document.Paint(-1,-1);return;}
    const int x=static_cast<int>(std::floor(world[0]/CellWidth)),y=static_cast<int>(std::floor(world[1]/CellWidth));
    if(x<0||y<0||x>=TerrainSide||y>=TerrainSide)return;
    try
    {
        DrawOverlay(document,scene,x,y);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            TerrainBrushSettings settings;
            settings.Tool=tool_<4?static_cast<TerrainBrushTool>(tool_):(erase_?TerrainBrushTool::RemoveFlag:TerrainBrushTool::AddFlag);
            settings.Radius=radius_;settings.Strength=strength_;settings.Flag=tool_==5?4:1;
            settings.TargetHeight=sampleLevel_?document.Heights()[y*TerrainSide+x]:static_cast<std::uint8_t>(level_);
            document.Begin(settings);
        }
        if(document.Drawing())document.Paint(x,y);
    }
    catch(const std::exception& error) {message_=error.what();document.Cancel();waitRelease_=true;}
}
void TerrainBrushUI::DrawOverlay(const TerrainBrushDocument& doc,IWorldEditorScene& scene,int x,int y)
{
    const auto camera=scene.Camera();auto* draw=ImGui::GetBackgroundDrawList();
    const int range=std::max(radius_,showZones_?OverlayRange:radius_);
    for(int cy=std::max(0,y-range);cy<=std::min(TerrainSide-2,y+range);++cy)
        for(int cx=std::max(0,x-range);cx<=std::min(TerrainSide-2,x+range);++cx)
        {
            const bool brush=(cx-x)*(cx-x)+(cy-y)*(cy-y)<=radius_*radius_;
            const auto attr=doc.Attributes()[cy*TerrainSide+cx];
            if(!brush&&(!showZones_||(attr&5)==0))continue;
            constexpr int dx[4]{0,1,1,0},dy[4]{0,0,1,1};ImVec2 screen[4];bool visible=true;
            for(int corner=0;corner<4;++corner)
            {
                const int px=cx+dx[corner],py=cy+dy[corner];std::array<float,2> point{};
                visible=visible&&camera.Project({px*CellWidth,py*CellWidth,doc.Heights()[py*TerrainSide+px]*HeightUnit+OverlayLift},point);
                screen[corner]={point[0],point[1]};
            }
            if(!visible)continue;
            if(showZones_&&(attr&5))draw->AddConvexPolyFilled(screen,4,(attr&4)?BlockFill:SafeFill);
            if(brush)draw->AddPolyline(screen,4,BrushLine,ImDrawFlags_Closed,1.25f);
        }
}
}
