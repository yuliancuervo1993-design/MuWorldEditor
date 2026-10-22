#include "WorldTriggerDocument.h"
#include <iostream>
#include <stdexcept>
using namespace Silver::WorldEditor;
int main(){try{int p=0;auto c=[&](bool v,const char*n){if(!v)throw std::runtime_error(n);++p;std::cout<<"PASS "<<n<<'\n';};
WorldTriggerDocument d;d.Reset();auto id=d.Add("Boss Arena","PlayerEnterRegion",7);c(d.AddCondition(id,"EventActive:Kundun"),"condition");
c(d.AddAction(id,"SpawnMonster:275:1"),"action");c(d.AddAction(id,"ShowMessage:Boss incoming"),"second action");
c(d.CanUndo()&&d.UndoLabel()=="Add Trigger Action","history label");
c(d.MoveAction(id,1,-1),"reorder action");c(d.Find(id)->Actions.front()=="ShowMessage:Boss incoming","reorder applied");
c(d.RemoveAction(id,1),"remove action");c(d.Find(id)->Actions.size()==1,"remove applied");
WorldTrigger edited=*d.Find(id);edited.Event="PlayerStayRegion";edited.Enabled=false;c(d.Set(id,edited,"Edit Trigger Header"),"edit header");
c(d.Find(id)->Event=="PlayerStayRegion"&&!d.Find(id)->Enabled,"header applied");
c(d.Undo(),"undo header");c(d.Find(id)->Event=="PlayerEnterRegion"&&d.Find(id)->Enabled,"undo restores header");
c(d.Redo(),"redo header");c(d.Find(id)->Event=="PlayerStayRegion"&&!d.Find(id)->Enabled,"redo restores header");
d.MarkSaved();c(!d.Dirty(),"mark saved clean");
c(d.AddCondition(id,"PlayerLevel>=400"),"second condition");c(d.Dirty(),"new edit dirty");c(d.MoveCondition(id,1,-1),"reorder condition");
c(d.Find(id)->Conditions.front()=="PlayerLevel>=400","condition reorder applied");c(d.RemoveCondition(id,1),"remove condition");
auto bytes=d.Encode(0);WorldTriggerDocument x;x.Decode(bytes,0);auto*t=x.Find(id);c(t&&t->Region==7,"region ref");c(t&&t->Conditions.size()==1&&t->Actions.size()==1,"steps roundtrip");c(!x.Dirty(),"decoded clean");
bool bad=false;try{x.Decode(bytes,1);}catch(const std::invalid_argument&){bad=true;}c(bad,"reject other map");
std::cout<<"WORLD_TRIGGER_DOCUMENT_OK passed="<<p<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
