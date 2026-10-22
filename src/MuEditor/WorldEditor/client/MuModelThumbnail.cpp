#include "stdafx.h"
#ifdef _EDITOR
#include "MuModelThumbnail.h"
#include "Render/Models/ZzzBMD.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Silver::WorldEditor
{
std::vector<WorldModelThumbnail::Triangle> BuildModelThumbnail(const BMD& model)
{
    constexpr int MaximumTriangles = 200;
    std::vector<WorldModelThumbnail::Triangle> result;
    int total = 0;
    for (int i=0;i<model.NumMeshs;++i) total += std::max(0,static_cast<int>(model.Meshs[i].NumTriangles));
    const int step = std::max(1,(total+MaximumTriangles-1)/MaximumTriangles);
    std::array<float,2> low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high{-low[0],-low[1]};
    for (int i=0;i<model.NumMeshs && result.size()<MaximumTriangles;++i)
    {
        const auto& mesh=model.Meshs[i]; if(!mesh.Triangles || !mesh.Vertices)continue;
        for(int face=0;face<mesh.NumTriangles && result.size()<MaximumTriangles;face+=step)
        {
            WorldModelThumbnail::Triangle triangle; bool valid=true;
            for(int corner=0;corner<3;++corner)
            {
                const int index=mesh.Triangles[face].VertexIndex[corner];
                if(index<0 || index>=mesh.NumVertices){valid=false;break;}
                const auto& v=mesh.Vertices[index].Position;
                triangle[corner]={0.70710678f*(v[0]-v[1]),0.35f*(v[0]+v[1])-0.85f*v[2]};
                if(!std::isfinite(triangle[corner][0]) || !std::isfinite(triangle[corner][1])){valid=false;break;}
            }
            if(!valid)continue;
            for(const auto& point:triangle)for(int axis=0;axis<2;++axis){low[axis]=std::min(low[axis],point[axis]);high[axis]=std::max(high[axis],point[axis]);}
            result.push_back(triangle);
        }
    }
    const float extent=std::max(high[0]-low[0],high[1]-low[1]);
    if(!std::isfinite(extent)||extent<=0)return {};
    for(auto& triangle:result)for(auto& point:triangle)for(int axis=0;axis<2;++axis)
        point[axis]=0.5f+(point[axis]-(low[axis]+high[axis])*0.5f)/extent*0.85f;
    return result;
}
}
#endif
