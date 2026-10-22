#include "WorldTriggerDocument.h"
#include "json.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
namespace Silver::WorldEditor
{
namespace { using Json=nlohmann::json; constexpr std::size_t MaxTriggers=2048,MaxBytes=1024*1024,MaxText=160,MaxSteps=128,MaxHistory=256; }
std::string WorldTriggerDocument::Checked(std::string v,const char* f)
{
    if(v.empty()||v.size()>MaxText) throw std::invalid_argument(std::string("Trigger ")+f+" invalido.");
    return v;
}
void WorldTriggerDocument::Restore(const Snapshot& snapshot)
{
    entries_=snapshot.Entries; nextId_=snapshot.NextId; ++revision_;
}
void WorldTriggerDocument::PushHistory(Snapshot before,std::string label)
{
    if(cursor_<history_.size()) history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
    const auto beforeState=state_,afterState=++nextState_;
    history_.push_back({std::move(before),Capture(),std::move(label),beforeState,afterState});
    if(history_.size()>MaxHistory) history_.erase(history_.begin());
    cursor_=history_.size();state_=history_.back().AfterState;++historySerial_;++revision_;
}
void WorldTriggerDocument::Reset()
{
    entries_.clear();nextId_=1;history_.clear();cursor_=0;state_=nextState_=savedState_=0;savedEntries_=entries_;++revision_;
}
TriggerId WorldTriggerDocument::Add(std::string name,std::string event,RegionId region)
{
    if(entries_.size()>=MaxTriggers) throw std::length_error("Limite de Triggers alcanzado.");
    auto before=Capture();
    entries_.push_back({nextId_++,Checked(std::move(name),"nombre"),Checked(std::move(event),"event"),region,{},{},true});
    const auto id=entries_.back().Id;PushHistory(std::move(before),"Create Trigger");return id;
}
bool WorldTriggerDocument::Remove(TriggerId id)
{
    auto it=std::find_if(entries_.begin(),entries_.end(),[id](const auto& x){return x.Id==id;});
    if(it==entries_.end()) return false;auto before=Capture();entries_.erase(it);PushHistory(std::move(before),"Delete Trigger");return true;
}
bool WorldTriggerDocument::AddCondition(TriggerId id,std::string value)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t)return false;
    if(t->Conditions.size()>=MaxSteps)throw std::length_error("Demasiadas condiciones.");
    auto before=Capture();t=const_cast<WorldTrigger*>(Find(id));t->Conditions.push_back(Checked(std::move(value),"condition"));
    PushHistory(std::move(before),"Add Trigger Condition");return true;
}
bool WorldTriggerDocument::AddAction(TriggerId id,std::string value)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t)return false;
    if(t->Actions.size()>=MaxSteps)throw std::length_error("Demasiadas acciones.");
    auto before=Capture();t=const_cast<WorldTrigger*>(Find(id));t->Actions.push_back(Checked(std::move(value),"action"));
    PushHistory(std::move(before),"Add Trigger Action");return true;
}
bool WorldTriggerDocument::Set(TriggerId id,const WorldTrigger& value,std::string label)
{
    auto it=std::find_if(entries_.begin(),entries_.end(),[id](const auto& x){return x.Id==id;});if(it==entries_.end())return false;
    WorldTrigger changed=value;changed.Id=id;changed.Name=Checked(std::move(changed.Name),"nombre");changed.Event=Checked(std::move(changed.Event),"event");
    if(changed.Conditions.size()>MaxSteps||changed.Actions.size()>MaxSteps)throw std::length_error("Trigger excede limite de steps.");
    for(auto& x:changed.Conditions)x=Checked(std::move(x),"condition");for(auto& x:changed.Actions)x=Checked(std::move(x),"action");
    if(*it==changed)return false;auto before=Capture();*it=std::move(changed);PushHistory(std::move(before),std::move(label));return true;
}
bool WorldTriggerDocument::RemoveCondition(TriggerId id,std::size_t index)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t||index>=t->Conditions.size())return false;auto before=Capture();
    t=const_cast<WorldTrigger*>(Find(id));t->Conditions.erase(t->Conditions.begin()+static_cast<std::ptrdiff_t>(index));PushHistory(std::move(before),"Remove Trigger Condition");return true;
}
bool WorldTriggerDocument::RemoveAction(TriggerId id,std::size_t index)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t||index>=t->Actions.size())return false;auto before=Capture();
    t=const_cast<WorldTrigger*>(Find(id));t->Actions.erase(t->Actions.begin()+static_cast<std::ptrdiff_t>(index));PushHistory(std::move(before),"Remove Trigger Action");return true;
}
bool WorldTriggerDocument::MoveCondition(TriggerId id,std::size_t index,int direction)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t||index>=t->Conditions.size()||(direction!=-1&&direction!=1))return false;
    const auto target=static_cast<std::ptrdiff_t>(index)+direction;if(target<0||target>=static_cast<std::ptrdiff_t>(t->Conditions.size()))return false;
    auto before=Capture();t=const_cast<WorldTrigger*>(Find(id));std::swap(t->Conditions[index],t->Conditions[static_cast<std::size_t>(target)]);
    PushHistory(std::move(before),"Reorder Trigger Condition");return true;
}
bool WorldTriggerDocument::MoveAction(TriggerId id,std::size_t index,int direction)
{
    auto* t=const_cast<WorldTrigger*>(Find(id));if(!t||index>=t->Actions.size()||(direction!=-1&&direction!=1))return false;
    const auto target=static_cast<std::ptrdiff_t>(index)+direction;if(target<0||target>=static_cast<std::ptrdiff_t>(t->Actions.size()))return false;
    auto before=Capture();t=const_cast<WorldTrigger*>(Find(id));std::swap(t->Actions[index],t->Actions[static_cast<std::size_t>(target)]);
    PushHistory(std::move(before),"Reorder Trigger Action");return true;
}
bool WorldTriggerDocument::Undo()
{
    if(!CanUndo())return false;const auto& e=history_[--cursor_];Restore(e.Before);state_=e.BeforeState;return true;
}
bool WorldTriggerDocument::Redo()
{
    if(!CanRedo())return false;const auto& e=history_[cursor_++];Restore(e.After);state_=e.AfterState;return true;
}
const WorldTrigger* WorldTriggerDocument::Find(TriggerId id) const
{
    auto it=std::find_if(entries_.begin(),entries_.end(),[id](const auto& x){return x.Id==id;});return it==entries_.end()?nullptr:&*it;
}
std::vector<std::uint8_t> WorldTriggerDocument::Encode(int mapId) const
{
    Json values=Json::array();for(const auto& t:entries_)values.push_back({{"id",t.Id},{"name",t.Name},{"event",t.Event},{"regionId",t.Region},{"conditions",t.Conditions},{"actions",t.Actions},{"enabled",t.Enabled}});
    Json root{{"schema",Schema},{"mapId",mapId},{"triggers",values}};auto text=root.dump(2)+"\n";if(text.size()>MaxBytes)throw std::length_error("Triggers excede limite.");return {text.begin(),text.end()};
}
void WorldTriggerDocument::Decode(std::span<const std::uint8_t> bytes,int expectedMap)
{
    if(bytes.empty()||bytes.size()>MaxBytes)throw std::invalid_argument("Triggers fuera de limites.");Json root;
    try{root=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Triggers JSON invalido.");}
    if(!root.is_object()||root.value("schema","")!=Schema||root.value("mapId",-1)!=expectedMap)throw std::invalid_argument("Triggers schema/mapa incompatible.");
    if(!root.contains("triggers")||!root.at("triggers").is_array()||root.at("triggers").size()>MaxTriggers)throw std::invalid_argument("Lista Triggers invalida.");
    std::vector<WorldTrigger> parsed;std::unordered_set<TriggerId> ids;TriggerId next=1;
    for(const auto& v:root.at("triggers"))
    {
        WorldTrigger t;t.Id=v.value("id",NoTrigger);t.Name=Checked(v.value("name",std::string{}),"nombre");t.Event=Checked(v.value("event",std::string{}),"event");t.Region=v.value("regionId",NoRegion);t.Enabled=v.value("enabled",true);
        if(t.Id==NoTrigger||!ids.insert(t.Id).second)throw std::invalid_argument("Trigger ID invalido/duplicado.");
        if(!v.contains("conditions")||!v.at("conditions").is_array()||v.at("conditions").size()>MaxSteps||!v.contains("actions")||!v.at("actions").is_array()||v.at("actions").size()>MaxSteps)throw std::invalid_argument("Trigger steps invalidos.");
        for(const auto& x:v.at("conditions"))t.Conditions.push_back(Checked(x.get<std::string>(),"condition"));for(const auto& x:v.at("actions"))t.Actions.push_back(Checked(x.get<std::string>(),"action"));
        next=std::max(next,t.Id+1);parsed.push_back(std::move(t));
    }
    entries_=std::move(parsed);nextId_=next;history_.clear();cursor_=0;state_=nextState_=savedState_=0;savedEntries_=entries_;++revision_;
}
}
