#include "WorldCadLayout.h"
#include "WorldObjectDocument.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Silver::WorldEditor;
namespace
{
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void Reject(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}Check(failed,"Expected rejection");}
}
int main()
{
    try
    {
        WorldObjectRecord prototype;prototype.Type=7;prototype.Position={1000,1000,50};prototype.Scale=2;
        WorldCadLayout plan;plan.Points={{{1000,1000}},{{1200,1000}}};plan.Spacing=100;
        const auto line=plan.Generate(prototype);
        Check(line.size()==3&&line[1].Position[0]==1100&&line.back().Position[0]==1200,"line spacing/endpoints");
        Check(line[1].Type==7&&line[1].Scale==2&&line[1].Position[2]==50,"prototype preserved");
        auto decoded=WorldCadLayout::Decode(plan.Encode(0),0);
        Check(decoded.Generate(prototype)==line,"CAD document exact roundtrip");
        Reject([&]{WorldCadLayout::Decode(plan.Encode(0),1);});
        WorldObjectFile file;WorldObjectDocument doc;doc.Load(file);
        const auto ids=doc.AddMany(line,"CAD line");Check(ids.size()==3&&doc.CanUndo(),"CAD transaction");
        Check(doc.Undo()&&doc.Export().Records.empty(),"one undo removes entire CAD batch");
        Check(doc.Redo()&&doc.Export().Records.size()==3,"redo restores entire CAD batch");
        plan.Kind=WorldCadLayout::Shape::Rectangle;plan.Points[1][1]=1200;
        Check(plan.Generate(prototype).size()==8&&plan.Length()==800,"rectangle perimeter no repeated closure");
        plan.Kind=WorldCadLayout::Shape::Circle;plan.Points.resize(1);plan.Radius=100;
        const auto circle=plan.Generate(prototype);Check(circle.size()==7,"circle bounded count");
        for(const auto& item:circle)Check(std::abs(std::hypot(item.Position[0]-1000,item.Position[1]-1000)-100)<0.01f,"circle radius");
        plan.Kind=WorldCadLayout::Shape::Polyline;plan.Points={{{1000,1000}},{{1100,1000}},{{1100,1200}}};
        Check(plan.Generate(prototype).size()==4&&plan.Length()==300,"polyline arclength distribution");
        plan.AlignToPath=true;Check(std::abs(plan.Generate(prototype).back().Angle[2]-90)<0.01f,"tangent rotation");
        plan.Spacing=1;Reject([&]{plan.Generate(prototype);});
        plan.Spacing=std::numeric_limits<float>::quiet_NaN();Reject([&]{plan.Generate(prototype);});
        plan.Spacing=100;plan.Points[0][0]=-1;Reject([&]{plan.Generate(prototype);});
        std::cout<<"CadLayoutTests PASS: four shapes, validation, persistence, atomic undo/redo\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
