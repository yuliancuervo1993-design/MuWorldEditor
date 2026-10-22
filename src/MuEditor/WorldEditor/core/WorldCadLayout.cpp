#include "WorldCadLayout.h"
#include "json.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
constexpr const char* Schema = "silver-world-editor-cad-v1";
constexpr float MinimumSegmentLength = 0.001f;
float Distance(const WorldCadLayout::Point& a,const WorldCadLayout::Point& b)
{ return std::hypot(b[0]-a[0],b[1]-a[1]); }
}

void WorldCadLayout::Validate() const
{
    if(Kind<Shape::Line||Kind>Shape::Circle||Points.empty()||Points.size()>MaximumPoints)
        throw std::invalid_argument("Forma CAD o cantidad de puntos invalida.");
    if((Kind==Shape::Circle&&Points.size()!=1)||(Kind!=Shape::Circle&&Points.size()<2)
        ||((Kind==Shape::Line||Kind==Shape::Rectangle)&&Points.size()!=2))
        throw std::invalid_argument("La forma CAD necesita uno o dos puntos, o una polilinea.");
    if(!std::isfinite(Spacing)||Spacing<1||!std::isfinite(Radius)||Radius<=0)
        throw std::invalid_argument("Separacion/radio CAD invalidos.");
    for(const auto& p:Points)
        for(const float value:p)
            if(!std::isfinite(value)||value<0||value>=MapEdge)
                throw std::invalid_argument("Punto CAD fuera del mapa.");
    if(Kind==Shape::Circle)
        for(const float value:Points.front())
            if(value-Radius<0||value+Radius>=MapEdge)throw std::invalid_argument("Circulo CAD fuera del mapa.");
    if(Kind==Shape::Rectangle&&(Points[0][0]==Points[1][0]||Points[0][1]==Points[1][1]))
        throw std::invalid_argument("Rectangulo CAD sin area.");
}

std::vector<WorldCadLayout::Point> WorldCadLayout::Route() const
{
    Validate();
    if(Kind!=Shape::Rectangle)return Points;
    const auto& a=Points[0];const auto& b=Points[1];
    return {a,{b[0],a[1]},b,{a[0],b[1]},a};
}

float WorldCadLayout::Length() const
{
    Validate();
    if(Kind==Shape::Circle)return 2*std::numbers::pi_v<float>*Radius;
    const auto route=Route();float length=0;
    for(std::size_t i=1;i<route.size();++i)
    {
        const auto segment=Distance(route[i-1],route[i]);
        if(segment<MinimumSegmentLength)throw std::invalid_argument("Segmento CAD sin longitud.");
        length+=segment;
    }
    return length;
}

std::vector<WorldObjectRecord> WorldCadLayout::Generate(const WorldObjectRecord& prototype) const
{
    ValidateRecord(prototype);
    if(prototype.BaselineOnly)throw std::invalid_argument("Este modelo legacy no admite duplicacion CAD.");
    const auto length=Length();
    const bool closed=Kind==Shape::Circle||Kind==Shape::Rectangle;
    const double steps=std::ceil(static_cast<double>(length)/Spacing);
    if(steps+(closed?0:1)>MaximumInstances)throw std::length_error("CAD supera 256 instancias; aumente la separacion.");
    const auto count=static_cast<std::size_t>(steps)+(closed?0:1);
    const auto route=Route();std::vector<WorldObjectRecord> result;
    for(std::size_t i=0;i<count;++i)
    {
        auto record=prototype;
        float angle=0;
        if(Kind==Shape::Circle)
        {
            const auto theta=2*std::numbers::pi_v<float>*static_cast<float>(i)/static_cast<float>(count);
            record.Position[0]=Points[0][0]+Radius*std::cos(theta);
            record.Position[1]=Points[0][1]+Radius*std::sin(theta);
            angle=theta+std::numbers::pi_v<float>/2;
        }
        else
        {
            float remaining=std::min(static_cast<float>(i)*Spacing,length);
            std::size_t segment=1;
            while(segment+1<route.size()&&remaining>Distance(route[segment-1],route[segment]))
            {remaining-=Distance(route[segment-1],route[segment]);++segment;}
            const auto& a=route[segment-1];const auto& b=route[segment];
            const auto fraction=remaining/Distance(a,b);
            record.Position[0]=a[0]+(b[0]-a[0])*fraction;
            record.Position[1]=a[1]+(b[1]-a[1])*fraction;
            angle=std::atan2(b[1]-a[1],b[0]-a[0]);
        }
        if(AlignToPath)record.Angle[2]+=angle*180/std::numbers::pi_v<float>;
        (void)SpatialBlock(record);result.push_back(record);
    }
    return result;
}

std::vector<std::uint8_t> WorldCadLayout::Encode(int mapId) const
{
    (void)Length();
    if(mapId<0)throw std::invalid_argument("Mapa CAD invalido.");
    const nlohmann::json json{{"schema",Schema},{"mapId",mapId},{"shape",static_cast<int>(Kind)},
        {"points",Points},{"spacing",Spacing},{"radius",Radius},{"align",AlignToPath}};
    const auto text=json.dump(2);return {text.begin(),text.end()};
}

WorldCadLayout WorldCadLayout::Decode(std::span<const std::uint8_t> bytes,int mapId)
{
    if(bytes.empty()||bytes.size()>16384)throw std::length_error("Documento CAD fuera de limites.");
    const auto json=nlohmann::json::parse(bytes.begin(),bytes.end());
    if(json.at("schema")!=Schema||json.at("mapId")!=mapId||mapId<0)
        throw std::invalid_argument("Documento CAD incompatible o de otro mapa.");
    WorldCadLayout result;result.Kind=static_cast<Shape>(json.at("shape").get<int>());
    result.Points=json.at("points").get<std::vector<Point>>();result.Spacing=json.at("spacing");
    result.Radius=json.at("radius");result.AlignToPath=json.at("align");(void)result.Length();return result;
}
}
