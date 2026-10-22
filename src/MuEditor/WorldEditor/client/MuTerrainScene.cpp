#include "stdafx.h"
#ifdef _EDITOR
#include "MuTerrainScene.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "World/MapInfra/MapManager.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
extern vec3_t TerrainNormal[];
namespace Silver::WorldEditor
{
namespace { constexpr std::uint16_t EditableFlags = 1 | 4 | 8 | 16 | 32 | 64; }
void MuTerrainScene::Attach(const WorldTerrainDocument& document)
{
    if (gMapManager.WorldActive < 0) throw std::runtime_error("No hay un mapa cargado para editar.");
    for (int i = 0; i < TerrainCells; ++i)
    {
        if (std::abs(BackTerrainHeight[i] - document.Heights()[i] * HeightUnit) > 0.01f)
            throw std::runtime_error("Las alturas cargadas no coinciden con el OZB. No se modifico el terreno.");
        if ((TerrainWall[i] & EditableFlags) != (document.Attributes().Cells[i] & EditableFlags))
            throw std::runtime_error("Las zonas cargadas difieren del archivo ATT. No se modifico el terreno.");
        if (document.Mapping())
        {
            const auto& mapping = *document.Mapping();
            if (TerrainMappingLayer1[i] != mapping.Layer1[i]
                || TerrainMappingLayer2[i] != mapping.Layer2[i]
                || std::abs(TerrainMappingAlpha[i] - static_cast<float>(mapping.Alpha[i]) / 255.0f) > 0.0001f)
                throw std::runtime_error("Los materiales cargados difieren del archivo MAP. No se modifico el terreno.");
        }
    }
    heights_ = document.Heights(); attributes_ = document.Attributes().Cells;
    mapping_ = document.Mapping();
    normalsDirty_.assign(TerrainCells, 0); revision_ = document.Revision();
    worldId_ = gMapManager.WorldActive; attached_ = true;
}
void MuTerrainScene::RebuildNormal(int index)
{
    const int x = index % TerrainSide, y = index / TerrainSide;
    vec3_t a, b, c, d, first, second;
    Vector(x * TERRAIN_SCALE, y * TERRAIN_SCALE, BackTerrainHeight[TERRAIN_INDEX_REPEAT(x,y)], d);
    Vector((x+1) * TERRAIN_SCALE, y * TERRAIN_SCALE, BackTerrainHeight[TERRAIN_INDEX_REPEAT(x+1,y)], a);
    Vector((x+1) * TERRAIN_SCALE, (y+1) * TERRAIN_SCALE, BackTerrainHeight[TERRAIN_INDEX_REPEAT(x+1,y+1)], b);
    Vector(x * TERRAIN_SCALE, (y+1) * TERRAIN_SCALE, BackTerrainHeight[TERRAIN_INDEX_REPEAT(x,y+1)], c);
    FaceNormalize(a,b,c,first); FaceNormalize(c,d,a,second);
    VectorAdd(first,second,TerrainNormal[index]);
}
void MuTerrainScene::Synchronize(const WorldTerrainDocument& document)
{
    if (!attached_ || gMapManager.WorldActive != worldId_) throw std::runtime_error("Escena de terreno invalidada.");
    if (revision_ == document.Revision()) return;
    bool heightChanged = false;
    std::fill(normalsDirty_.begin(), normalsDirty_.end(), static_cast<std::uint8_t>(0));
    if (document.Mapping() && !mapping_)
        throw std::runtime_error("El mapa de materiales de la escena no esta preparado.");
    for (int i = 0; i < TerrainCells; ++i)
    {
        const auto flags = document.Attributes().Cells[i];
        const auto mask = static_cast<std::uint16_t>((flags ^ attributes_[i]) & EditableFlags);
        TerrainWall[i] = static_cast<WORD>((TerrainWall[i] & ~mask) | (flags & mask));
        attributes_[i] = flags;
        if (document.Mapping())
        {
            const auto& mapping = *document.Mapping();
            TerrainMappingLayer1[i] = mapping.Layer1[i];
            TerrainMappingLayer2[i] = mapping.Layer2[i];
            TerrainMappingAlpha[i] = static_cast<float>(mapping.Alpha[i]) / 255.0f;
            mapping_->Layer1[i] = mapping.Layer1[i];
            mapping_->Layer2[i] = mapping.Layer2[i];
            mapping_->Alpha[i] = mapping.Alpha[i];
        }
        if (heights_[i] == document.Heights()[i]) continue;
        heights_[i] = document.Heights()[i]; heightChanged = true;
        BackTerrainHeight[i] = PrimaryTerrainHeight[i] = heights_[i] * HeightUnit;
        const int x = i % TerrainSide, y = i / TerrainSide;
        for (int dy = -1; dy <= 0; ++dy) for (int dx = -1; dx <= 0; ++dx)
            normalsDirty_[TERRAIN_INDEX_REPEAT(x+dx,y+dy)] = 1;
    }
    if (heightChanged)
    {
        for (int i = 0; i < TerrainCells; ++i) if (normalsDirty_[i]) RebuildNormal(i);
        CreateTerrainLight();
    }
    revision_ = document.Revision();
}
}
#endif
