#include "WorldTriggerFlowUI.h"
#include <algorithm>
#include <cstdio>

namespace Silver::WorldEditor
{
bool WorldTriggerFlowUI::Node(const char* title,const std::string& value,int kind,std::size_t index,
    const ImVec2& origin,float width,float& y,ImVec2& previous,bool& connected)
{
    constexpr float height=46,gap=18;
    auto* draw=ImGui::GetWindowDrawList();const ImVec2 a{origin.x+20,y},b{a.x+width,y+height};
    if(connected)draw->AddBezierCubic(previous,{previous.x+35,previous.y},{a.x-35,y+height/2},
        {a.x,y+height/2},IM_COL32(150,190,220,220),2);
    const ImU32 colors[]{IM_COL32(52,82,120,235),IM_COL32(104,84,48,235),IM_COL32(96,58,94,235),IM_COL32(58,105,88,235)};
    draw->AddRectFilled(a,b,colors[kind],6);draw->AddRect(a,b,IM_COL32(210,220,230,190),6);
    draw->PushClipRect(a,b,true);draw->AddText({a.x+10,a.y+5},IM_COL32(235,240,245,255),title);
    draw->AddText({a.x+10,a.y+24},IM_COL32(225,225,225,255),value.c_str());draw->PopClipRect();
    ImGui::SetCursorScreenPos(a);ImGui::PushID(kind);ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("node",{width,height});const bool clicked=ImGui::IsItemClicked();
    if(kind!=3&&ImGui::BeginPopupContextItem("edit-node"))
    {
        if(ImGui::IsWindowAppearing())std::snprintf(text_,sizeof(text_),"%s",value.c_str());
        ImGui::InputText("Valor",text_,sizeof(text_));
        const auto select=[&](Command command){command_=command;kind_=kind;index_=index;ImGui::CloseCurrentPopup();};
        if(ImGui::Button("Aplicar texto"))select(Command::Edit);
        if(kind!=0)
        {
            if(ImGui::Button("Subir"))select(Command::Up);
            ImGui::SameLine();if(ImGui::Button("Bajar"))select(Command::Down);
            if(ImGui::Button("Quitar nodo"))select(Command::Remove);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();ImGui::PopID();previous={b.x,y+height/2};connected=true;y+=height+gap;
    return clicked;
}

bool WorldTriggerFlowUI::Apply(WorldTriggerDocument& document,TriggerId id)
{
    const auto* current=document.Find(id);if(!current||command_==Command::None)return false;
    auto changed=*current;
    if(kind_==0)
    {if(command_!=Command::Edit)return false;changed.Event=text_;}
    else
    {
        auto& steps=kind_==1?changed.Conditions:changed.Actions;
        if(index_>=steps.size())return false;
        if(command_==Command::Edit)steps[index_]=text_;
        if(command_==Command::Remove)steps.erase(steps.begin()+static_cast<std::ptrdiff_t>(index_));
        if(command_==Command::Up){if(index_==0)return false;std::swap(steps[index_],steps[index_-1]);}
        if(command_==Command::Down){if(index_+1==steps.size())return false;std::swap(steps[index_],steps[index_+1]);}
    }
    return document.Set(id,changed,"Edit Trigger Graph Node");
}

WorldTriggerFlowUI::Result WorldTriggerFlowUI::Draw(WorldTriggerDocument& document,TriggerId id,const WorldRegionDocument& regions)
{
    Result result;command_=Command::None;const auto* trigger=document.Find(id);if(!trigger)return result;
    ImGui::PushID(static_cast<int>(id));
    ImGui::SeparatorText("Flujo de trigger | clic derecho para editar");
    const float height=std::clamp(130.0f+64.0f*static_cast<float>(trigger->Conditions.size()+trigger->Actions.size()),220.0f,520.0f);
    if(ImGui::BeginChild("trigger-flow",{0,height},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar))
    {
        const auto origin=ImGui::GetCursorScreenPos();float y=origin.y+8;
        const float width=std::max(180.0f,ImGui::GetContentRegionAvail().x-56);
        ImVec2 previous{};bool connected=false;
        Node("EVENT",trigger->Event,0,0,origin,width,y,previous,connected);
        if(trigger->Region!=NoRegion)
        {
            const auto* region=regions.Find(trigger->Region);
            if(Node("REGION",region?region->Name:"Region faltante",3,0,origin,width,y,previous,connected)&&region)
                result.FocusRegion=region->Id;
        }
        for(std::size_t i=0;i<trigger->Conditions.size();++i)
            Node("IF",trigger->Conditions[i],1,i,origin,width,y,previous,connected);
        for(std::size_t i=0;i<trigger->Actions.size();++i)
            Node("DO",trigger->Actions[i],2,i,origin,width,y,previous,connected);
        ImGui::SetCursorScreenPos({origin.x,y});ImGui::Dummy({width+40,8});
    }
    ImGui::EndChild();ImGui::PopID();
    try{result.Changed=Apply(document,id);if(result.Changed)status_.clear();}
    catch(const std::exception& error){status_=error.what();}
    if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
    ImGui::TextDisabled("Cada edicion se puede deshacer. Las conexiones muestran el orden; no ejecuta OpenMU.");
    return result;
}
}
