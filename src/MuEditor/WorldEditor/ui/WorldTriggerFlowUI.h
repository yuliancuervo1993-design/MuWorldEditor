#pragma once
#include "../core/WorldTriggerDocument.h"
#include "imgui.h"

namespace Silver::WorldEditor
{
class WorldTriggerFlowUI
{
public:
    struct Result { bool Changed=false; RegionId FocusRegion=NoRegion; };
    Result Draw(WorldTriggerDocument& document, TriggerId id, const WorldRegionDocument& regions);
private:
    enum class Command { None, Edit, Up, Down, Remove };
    bool Node(const char* title,const std::string& value,int kind,std::size_t index,
        const ImVec2& origin,float width,float& y,ImVec2& previous,bool& connected);
    bool Apply(WorldTriggerDocument& document, TriggerId id);
    Command command_=Command::None;
    int kind_=0;
    std::size_t index_=0;
    char text_[161]{};
    std::string status_;
};
}
