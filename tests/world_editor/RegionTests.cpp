#include "WorldRegionDocument.h"
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;
int main()
{
    try
    {
        int passed=0;
        auto check=[&](bool v,const char* n){if(!v)throw std::runtime_error(n);++passed;std::cout<<"PASS "<<n<<'\n';};
        WorldRegionDocument doc;
        doc.Reset();
        const auto a=doc.AddRectangle("ArenaBoss",100,200,500,600,"boss");
        const auto b=doc.AddCircle("SafeZone",800,900,150,"safe");
        check(doc.Dirty(),"region edits are dirty");
        const auto bytes=doc.Encode(0);
        WorldRegionDocument restored;
        restored.Decode(bytes,0);
        check(restored.Entries().size()==2,"regions roundtrip");
        check(restored.Find(a)&&restored.Find(a)->Name=="ArenaBoss","rectangle restored");
        check(restored.Find(b)&&restored.Find(b)->Radius==150.0f,"circle restored");
        check(!restored.Dirty(),"decoded candidate is clean");
        auto edited=*restored.Find(a); edited.X1=125.0f; edited.Y1=225.0f;
        check(restored.Set(a,edited,"Move Region"),"region inspector edit");
        check(restored.CanUndo()&&restored.UndoLabel()=="Move Region","region edit exposes undo label");
        check(restored.Undo()&&restored.Find(a)->X1==100.0f,"region undo restores geometry");
        check(restored.Redo()&&restored.Find(a)->X1==125.0f,"region redo restores edit");
        restored.BeginEdit(b,"Resize Circle Region");
        auto circle=*restored.Find(b); circle.Radius=225.0f;
        check(restored.Preview(circle)&&restored.Find(b)->Radius==225.0f,"interactive region preview");
        check(restored.CommitEdit()&&restored.UndoLabel()=="Resize Circle Region","interactive region commit is one transaction");
        check(restored.Undo()&&restored.Find(b)->Radius==150.0f,"interactive region undo is atomic");
        restored.MarkSaved();
        check(!restored.Dirty(),"mark saved tracks history state");
        bool rejected=false; try{restored.Decode(bytes,1);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"regions reject another map");
        check(restored.Remove(a),"region remove");
        std::cout<<"WORLD_REGION_DOCUMENT_OK passed="<<passed<<'\n';
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
