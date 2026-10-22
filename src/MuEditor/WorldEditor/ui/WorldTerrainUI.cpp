#include "WorldTerrainUI.h"
#include "../core/TerrainBrushFootprint.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
namespace Silver::WorldEditor
{
namespace
{
constexpr float TileSize = 100.0f;
constexpr float OverlayLift = 3.0f;
constexpr int OverlayRadius = 24;
constexpr ImU32 SafeColor = IM_COL32(30,220,105,65);
constexpr ImU32 BlockColor = IM_COL32(245,70,65,90);
constexpr ImU32 CombinedColor = IM_COL32(245,170,40,100);
constexpr ImU32 BrushColor = IM_COL32(245,230,110,230);
}
void WorldTerrainUI::Cancel(WorldTerrainDocument& document)
{
    document.Cancel(); hasHit_ = false; rampAnchor_.reset();
}
bool WorldTerrainUI::Draw(WorldTerrainDocument& document, IWorldEditorScene& scene, bool* open, bool resetLayout)
{
    bool save = false;
    if (!open || *open)
    {
    ImGui::SetNextWindowPos({8, 148}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({380, 520}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Terreno, Safe Zone y bloqueos", open)) Controls(document, scene, save);
    ImGui::End();
    }
    Input(document, scene);
    Overlay(document, scene);
    return save;
}
void WorldTerrainUI::SizeControls()
{
    if (ImGui::Button("-##radio")) brush_.Radius = std::max(0, brush_.Radius - 1);
    ImGui::SameLine(); if (ImGui::Button("+##radio")) brush_.Radius = std::min(16, brush_.Radius + 1);
    ImGui::SameLine(); ImGui::Text("Radio: %d celdas", brush_.Radius);
    ImGui::SliderInt("Tamano del pincel", &brush_.Radius, 0, 16);
    for (const int radius : {1, 3, 6, 12})
    {
        ImGui::PushID(radius);
        if (ImGui::Button(std::to_string(radius).c_str())) brush_.Radius = radius;
        ImGui::PopID(); ImGui::SameLine();
    }
    ImGui::NewLine();
    if (brush_.Mode == TerrainBrushMode::Raise || brush_.Mode == TerrainBrushMode::Lower
        || brush_.Mode == TerrainBrushMode::Noise || brush_.Mode == TerrainBrushMode::Terrace
        || brush_.Mode == TerrainBrushMode::Erode)
        ImGui::SliderInt(brush_.Mode == TerrainBrushMode::Terrace ? "Paso de terraza" : "Fuerza", &brush_.Strength, 1, 32);
    if (brush_.Mode == TerrainBrushMode::Raise || brush_.Mode == TerrainBrushMode::Lower
        || brush_.Mode == TerrainBrushMode::Noise || brush_.Mode == TerrainBrushMode::Erode)
    {
        ImGui::SliderFloat("Falloff", &brush_.Falloff, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("0 = fuerza uniforme | 1 = caida completa hacia el borde.");
    }
    ImGui::TextWrapped("Rueda: zoom de camara. Ctrl + rueda: tamano del pincel. Shift + rueda: fuerza al subir/bajar.");
}
void WorldTerrainUI::Controls(WorldTerrainDocument& document, IWorldEditorScene& scene, bool& save)
{
    ImGui::TextWrapped("Elegir herramienta y arrastrar sobre el suelo. Esc cancela. Alt + clic usa eyedropper: altura o material segun la herramienta.");
    ImGui::BeginDisabled(document.Active());
    int mode = static_cast<int>(brush_.Mode);
    const char* names[]{"Subir terreno", "Bajar terreno", "Aplanar", "Suavizar", "Pintar zonas", "Borrar zonas",
        "Pintar material base", "Pintar material de mezcla", "Mezclar material", "Borrar mezcla", "Ruido", "Terrazas",
        "Erosion", "Rellenar material", "Rampa"};
    const bool reliefMode = mode < 4 || (mode >= 10 && mode <= 12) || mode == 14;
    if (ImGui::RadioButton("Relieve", reliefMode)) mode = 0;
    ImGui::SameLine(); if (ImGui::RadioButton("Zonas", mode >= 4 && mode < 6)) mode = 4;
    ImGui::SameLine(); if (ImGui::RadioButton("Materiales", (mode >= 6 && mode < 10) || mode == 13)) mode = 6;
    ImGui::Separator();
    const int first = reliefMode ? 0 : (mode < 6 ? 4 : 6);
    const int last = reliefMode ? 4 : (mode < 6 ? 6 : 10);
    for (int i = first; i < last; ++i)
    { ImGui::RadioButton(names[i], &mode, i); if (i % 2 == 0) ImGui::SameLine(); }
    if (reliefMode)
    {
        ImGui::RadioButton(names[10], &mode, 10); ImGui::SameLine();
        ImGui::RadioButton(names[11], &mode, 11); ImGui::SameLine();
        ImGui::RadioButton(names[12], &mode, 12); ImGui::SameLine();
        ImGui::RadioButton(names[14], &mode, 14);
    }
    else if (mode >= 6)
    {
        ImGui::RadioButton(names[13], &mode, 13);
    }
    brush_.Mode = static_cast<TerrainBrushMode>(mode);
    if (brush_.Mode != TerrainBrushMode::Ramp) rampAnchor_.reset();
    switch (brush_.Mode)
    {
    case TerrainBrushMode::Raise: ImGui::TextWrapped("Subir: eleva el terreno dentro del circulo del pincel."); break;
    case TerrainBrushMode::Lower: ImGui::TextWrapped("Bajar: hunde el terreno dentro del circulo del pincel."); break;
    case TerrainBrushMode::Flatten: ImGui::TextWrapped("Aplanar: lleva el suelo al nivel elegido. Alt + clic toma la altura del punto bajo el mouse."); break;
    case TerrainBrushMode::Smooth: ImGui::TextWrapped("Suavizar: promedia alturas vecinas para quitar cortes bruscos."); break;
    case TerrainBrushMode::AddFlags: ImGui::TextWrapped("Pintar zonas: agrega los flags marcados, por ejemplo Safe Zone o bloqueo de paso."); break;
    case TerrainBrushMode::RemoveFlags: ImGui::TextWrapped("Borrar zonas: quita solamente los flags marcados, sin tocar los demas."); break;
    case TerrainBrushMode::PaintLayer1: ImGui::TextWrapped("Material base: reemplaza la textura principal de cada celda."); break;
    case TerrainBrushMode::PaintLayer2: ImGui::TextWrapped("Material de mezcla: cambia la segunda textura sin modificar su alpha."); break;
    case TerrainBrushMode::BlendMaterial: ImGui::TextWrapped("Mezclar: cambia la segunda textura y su alpha de mezcla."); break;
    case TerrainBrushMode::EraseBlend: ImGui::TextWrapped("Borrar mezcla: vuelve la segunda capa al valor neutro y alpha cero."); break;
    case TerrainBrushMode::Noise: ImGui::TextWrapped("Ruido: variacion determinista de altura para romper superficies demasiado planas."); break;
    case TerrainBrushMode::Terrace: ImGui::TextWrapped("Terrazas: cuantiza alturas en escalones regulares segun el paso configurado."); break;
    case TerrainBrushMode::Erode: ImGui::TextWrapped("Erosion: reduce picos hacia la altura media vecina, limitada por la fuerza del pincel."); break;
    case TerrainBrushMode::FillLayer1: ImGui::TextWrapped("Rellenar material: reemplaza el area contigua del material base bajo el clic como una sola pincelada."); break;
    case TerrainBrushMode::Ramp: ImGui::TextWrapped("Rampa: clic en inicio y luego clic en final. Interpola la altura entre ambos puntos con el ancho del pincel."); break;
    }
    SizeControls();
    int flatten = brush_.FlattenHeight;
    if (brush_.Mode == TerrainBrushMode::Flatten) ImGui::SliderInt("Nivel para aplanar", &flatten, 0, 255);
    brush_.FlattenHeight = static_cast<std::uint8_t>(flatten);
    const bool materialControls = (mode >= 6 && mode < 10) || mode == 13;
    if (materialControls)
    {
        int material = brush_.Material;
        if (brush_.Mode != TerrainBrushMode::EraseBlend)
            ImGui::SliderInt("Indice de material", &material, 0, 255);
        brush_.Material = material;
        if (brush_.Mode == TerrainBrushMode::BlendMaterial)
            ImGui::SliderInt("Alpha de mezcla", &brush_.BlendAlpha, 0, 255);
        if (!document.Mapping())
            ImGui::TextWrapped("Este mapa no expuso EncTerrainN.map al editor; el pincel de materiales queda bloqueado.");
        else
        {
            ImGui::Checkbox("Preview real del material", &showMaterialPreview_);
            ImGui::SliderFloat("Opacidad del preview", &materialPreviewOpacity_, 0.10f, 0.90f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Mostrar zonas durante material", &showZonesDuringMaterial_);
            MaterialLibrary(document, scene);
        }
    }
    ImGui::Text("Altura: %.1f / %.1f unidades", flatten * HeightUnit, 255 * HeightUnit);
    ImGui::SeparatorText("Zonas (sin escribir numeros de flags)");
    struct Flag { const char* Name; std::uint16_t Value; };
    if (mode >= 4 && mode < 6) for (const auto flag : {Flag{"Zona segura (Safe Zone)",1}, {"Bloquear paso",4}, {"Sin suelo",8}, {"Agua",16}})
    {
        bool selected = (brush_.Flags & flag.Value) != 0;
        if (ImGui::Checkbox(flag.Name, &selected))
            brush_.Flags = selected ? static_cast<std::uint16_t>(brush_.Flags | flag.Value) : static_cast<std::uint16_t>(brush_.Flags & ~flag.Value);
    }
    ImGui::Checkbox("Pincel habilitado", &enabled_);
    ImGui::EndDisabled();
    ImGui::Checkbox("Mostrar zonas sobre el suelo", &showZones_);
    ImGui::TextDisabled("Undo/Redo: History global (Ctrl+Z / Ctrl+Y).");
    ImGui::BeginDisabled(document.Active()); if (ImGui::Button("Guardar copia completa")) save = true; ImGui::EndDisabled();
    ImGui::TextUnformatted(document.Dirty() ? "Terreno con cambios sin guardar" : "Terreno sin cambios pendientes");
    ImGui::TextWrapped("Verde: segura. Rojo: bloqueo. Naranja: ambos. Azul: agua. Violeta: sin suelo. En Lorencia, la celda de control 135,123 se protege automaticamente.");
    ImGui::TextWrapped("Vista de cliente LAB. Safe Zone real para jugadores requiere integrar tambien el mapa del servidor.");
    ImGui::TextWrapped("%s", message_.c_str());
}
void WorldTerrainUI::MaterialLibrary(WorldTerrainDocument& document, IWorldEditorScene& scene)
{
    constexpr const char* Categories[]{"Todos","Cesped","Suelo","Agua","Madera","Roca","Extendido","Especial"};
    if (!registryAttempted_)
    {
        registryAttempted_ = true;
        registryError_.clear();
        try { registry_.Scan("Data"); }
        catch (const std::exception& error) { registryError_ = error.what(); }
    }

    ImGui::SeparatorText("Biblioteca global de suelos");
    ImGui::Text("%zu materiales paintables | %zu WorldN indexados",
        registry_.TerrainMaterials().size(), registry_.TerrainWorldCount());
    ImGui::TextDisabled("Cada tarjeta es una textura real de su World. Otros mapas usan un slot extendido libre; nunca reemplazan suelos ya pintados.");

    bool changed = ImGui::InputTextWithHint("##buscar-suelo-global", "Buscar grass, rock, water, ExtTile...", materialSearch_, sizeof(materialSearch_));
    changed = ImGui::Combo("Tipo de suelo", &materialCategory_, Categories, 8) || changed;
    (void)changed;
    if (ImGui::Button("Actualizar Global Asset Registry"))
    {
        try
        {
            registry_.Scan("Data");
            registryError_.clear();
            message_ = "Global Asset Registry actualizado.";
        }
        catch (const std::exception& error) { registryError_ = error.what(); }
    }

    const auto search = WorldModelCatalog::Lower(materialSearch_);
    const int currentWorld = scene.WorldNumber();
    const auto currentSlotName = scene.TerrainMaterialRuntimeName(brush_.Material);
    ImGui::Text("Mapa actual: Map %d | World%d | material %d%s%s",
        scene.MapId(), currentWorld, brush_.Material,
        currentSlotName.empty() ? "" : " | ",
        currentSlotName.empty() ? "" : currentSlotName.c_str());

    if (ImGui::BeginChild("global-terrain-material-library", {0, 300}, ImGuiChildFlags_Borders))
    {
        const float cardWidth = 112.0f;
        const float cardHeight = 124.0f;
        const float gap = 8.0f;
        const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int columns = std::max(1, static_cast<int>(available / (cardWidth + gap)));
        int column = 0;

        for (const auto& asset : registry_.TerrainMaterials())
        {
            if (materialCategory_ != 0 && asset.Category != Categories[materialCategory_]) continue;
            const auto searchable = WorldModelCatalog::Lower(asset.Name + " " + asset.Key + " " + asset.Category);
            if (!search.empty() && searchable.find(search) == std::string::npos) continue;

            const bool listedForWorld = currentWorld > 0
                && std::binary_search(asset.Worlds.begin(), asset.Worlds.end(), currentWorld);
            const auto runtimeName = scene.TerrainMaterialRuntimeName(asset.Slot);
            const bool compatible = listedForWorld && scene.TerrainMaterialMatches(asset.Slot, asset.Identity);
            const auto thumbnail = !ImGui::IsRectVisible({cardWidth,cardHeight}) ? 0 : compatible
                ? scene.TerrainMaterialThumbnail(asset.Slot) : scene.LibraryTexturePreview(asset.Identity);

            ImGui::PushID(asset.Identity.c_str());
            const auto origin = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##terrain-global-card", {cardWidth, cardHeight});
            const auto end = ImVec2{origin.x + cardWidth, origin.y + cardHeight};
            auto* draw = ImGui::GetWindowDrawList();
            const bool selected = compatible && brush_.Material == asset.Slot;
            draw->AddRectFilled(origin, end,
                selected ? IM_COL32(55, 78, 91, 255) : IM_COL32(27, 34, 42, 255), 5.0f);
            draw->AddRect(origin, end,
                compatible ? IM_COL32(95, 190, 140, 220) : IM_COL32(80, 90, 100, 180), 5.0f);

            const ImVec2 previewMin{origin.x + 5.0f, origin.y + 5.0f};
            const ImVec2 previewMax{end.x - 5.0f, origin.y + 76.0f};
            if (thumbnail != 0)
                draw->AddImage(static_cast<ImTextureID>(thumbnail), previewMin, previewMax);
            else
            {
                draw->AddRectFilled(previewMin, previewMax, IM_COL32(20, 24, 29, 255), 3.0f);
                const std::string slot = "Slot " + std::to_string(asset.Slot);
                draw->AddText({previewMin.x + 8.0f, previewMin.y + 26.0f},
                    IM_COL32(190, 195, 202, 255), slot.c_str());
            }

            draw->AddText({origin.x + 5.0f, origin.y + 81.0f},
                IM_COL32(235, 235, 235, 255), asset.Name.c_str());
            const std::string originText = compatible
                ? "Disponible aqui"
                : std::to_string(asset.Worlds.size()) + " WorldN";
            draw->AddText({origin.x + 5.0f, origin.y + 101.0f},
                compatible ? IM_COL32(120, 220, 155, 255) : IM_COL32(150, 155, 165, 255),
                originText.c_str());

            if (clicked)
            {
                if (compatible)
                {
                    brush_.Material = asset.Slot;
                    message_ = "Material seleccionado: " + asset.Name + " | slot " + std::to_string(asset.Slot);
                }
                else
                {
                    try
                    {
                        const int slot = scene.UseLibraryTerrain(asset.Identity, *document.Mapping());
                        if (slot >= 0) { brush_.Material = slot; message_ = "Suelo preparado: " + asset.Name + ". Pinta sobre el mapa y guarda con Ctrl+S."; }
                    }
                    catch(const std::exception& error) { message_ = error.what(); }
                }
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s | %s | slot %d", asset.Name.c_str(), asset.Category.c_str(), asset.Slot);
                ImGui::Text("%zu archivos | %zu WorldN", asset.Occurrences, asset.Worlds.size());
                if (!runtimeName.empty()) ImGui::Text("Runtime actual del slot: %s", runtimeName.c_str());
                ImGui::TextUnformatted(compatible ? "Compatible con el mapa actual." : "Clic: cargar en un slot libre y elegir para pintar.");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
            ++column;
            if (column < columns) ImGui::SameLine(0.0f, gap);
            else column = 0;
        }
    }
    ImGui::EndChild();

    if (!registryError_.empty())
        ImGui::TextWrapped("Registry: %s", registryError_.c_str());
}

void WorldTerrainUI::Input(WorldTerrainDocument& document, IWorldEditorScene& scene)
{
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::GetIO().AppFocusLost)
    { document.Cancel(); rampAnchor_.reset(); hasHit_ = false; return; }
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !document.Active())
    { if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) document.Undo(); if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) document.Redo(); }
    if (document.Active() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) document.Commit();
    if (!enabled_ || ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
    { document.BreakStrokePath(); hasHit_ = false; return; }
    const auto& io = ImGui::GetIO();
    if (!document.Active() && !io.KeyAlt && std::isfinite(io.MouseWheel) && io.MouseWheel != 0.0f)
    {
        const int steps = static_cast<int>(std::round(std::clamp(io.MouseWheel, -16.0f, 16.0f)));
        if (io.KeyCtrl)
            brush_.Radius = std::clamp(brush_.Radius + steps, 0, 16);
        else if (io.KeyShift && (brush_.Mode == TerrainBrushMode::Raise || brush_.Mode == TerrainBrushMode::Lower
            || brush_.Mode == TerrainBrushMode::Noise || brush_.Mode == TerrainBrushMode::Terrace
            || brush_.Mode == TerrainBrushMode::Erode))
            brush_.Strength = std::clamp(brush_.Strength + steps, 1, 32);
        // Plain wheel is handled once at session level as camera zoom.
    }
    const auto mouse = ImGui::GetMousePos();
    hasHit_ = scene.Placement(mouse.x, mouse.y, hit_);
    if (!hasHit_) { document.BreakStrokePath(); return; }
    const int x = std::clamp(static_cast<int>(hit_[0] / TileSize), 0, TerrainSide - 1);
    const int y = std::clamp(static_cast<int>(hit_[1] / TileSize), 0, TerrainSide - 1);
    if (ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int index=y*TerrainSide+x;
        const bool materialMode=brush_.Mode==TerrainBrushMode::PaintLayer1
            || brush_.Mode==TerrainBrushMode::PaintLayer2 || brush_.Mode==TerrainBrushMode::BlendMaterial
            || brush_.Mode==TerrainBrushMode::FillLayer1;
        if(materialMode && document.Mapping())
        {
            const auto& mapping=*document.Mapping();
            if(brush_.Mode==TerrainBrushMode::PaintLayer1) brush_.Material=mapping.Layer1[index];
            else brush_.Material=mapping.Layer2[index];
            if(brush_.Mode==TerrainBrushMode::BlendMaterial) brush_.BlendAlpha=mapping.Alpha[index];
            message_="Material tomado del suelo con eyedropper.";
        }
        else
        {
            brush_.FlattenHeight=document.Heights()[index];
            message_="Altura tomada del suelo.";
        }
        return;
    }
    try
    {
        if (brush_.Mode == TerrainBrushMode::Ramp)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (!rampAnchor_)
                {
                    rampAnchor_ = std::array<int,2>{x,y};
                    message_ = "Rampa: punto inicial guardado. Hace clic en el punto final.";
                }
                else
                {
                    const auto start = *rampAnchor_;
                    rampAnchor_.reset();
                    if (document.Ramp(start[0], start[1], x, y, brush_.Radius))
                        message_ = "Rampa aplicada como una sola transaccion Undo/Redo.";
                    else
                        message_ = "La rampa no produjo cambios de altura.";
                }
            }
            return;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { document.Begin(brush_); message_.clear(); }
        if (document.Active()) document.Stamp(x,y);
    }
    catch (const std::exception& error) { document.Cancel(); message_ = error.what(); }
}
void WorldTerrainUI::Overlay(const WorldTerrainDocument& document, IWorldEditorScene& scene)
{
    if (!hasHit_) return;

    const bool materialMode = brush_.Mode == TerrainBrushMode::PaintLayer1
        || brush_.Mode == TerrainBrushMode::PaintLayer2
        || brush_.Mode == TerrainBrushMode::BlendMaterial
        || brush_.Mode == TerrainBrushMode::EraseBlend
        || brush_.Mode == TerrainBrushMode::FillLayer1;
    const bool showZoneOverlay = showZones_ && (!materialMode || showZonesDuringMaterial_);
    const auto materialTexture = materialMode && brush_.Mode != TerrainBrushMode::EraseBlend
        ? scene.TerrainMaterialThumbnail(brush_.Material) : 0;

    float previewAlpha = std::clamp(materialPreviewOpacity_, 0.10f, 0.90f);
    if (brush_.Mode == TerrainBrushMode::BlendMaterial)
        previewAlpha *= std::clamp(static_cast<float>(brush_.BlendAlpha) / 255.0f, 0.10f, 1.0f);
    const auto previewTint = IM_COL32(255, 255, 255,
        static_cast<int>(std::round(previewAlpha * 255.0f)));

    const auto camera = scene.Camera();
    const std::array<float,3> center = hit_;
    const int cx = std::clamp(static_cast<int>(center[0] / TileSize),0,255);
    const int cy = std::clamp(static_cast<int>(center[1] / TileSize),0,255);
    auto* draw = ImGui::GetBackgroundDrawList();

    const auto insideBrush = [&](int x, int y)
    {
        return x >= 0 && y >= 0 && x < TerrainSide && y < TerrainSide
            && TerrainBrushFootprint::Contains(x, y, cx, cy, brush_.Radius);
    };

    for (int y = std::max(0,cy-OverlayRadius); y < std::min(TerrainSide,cy+OverlayRadius); ++y)
        for (int x = std::max(0,cx-OverlayRadius); x < std::min(TerrainSide,cx+OverlayRadius); ++x)
        {
            const auto flags = document.Flags(x,y);
            const bool brushCell = insideBrush(x,y);
            if ((!showZoneOverlay || !(flags&29)) && !brushCell) continue;

            std::array<std::array<float,2>,4> points{};
            constexpr int offsets[4][2]{{0,0},{1,0},{1,1},{0,1}};
            bool visible=true;
            for(int i=0;i<4;++i)
            {
                const int px=x+offsets[i][0], py=y+offsets[i][1];
                visible = camera.Project(
                    {px*TileSize,py*TileSize,document.Height(px & 255,py & 255)+OverlayLift},
                    points[i]) && visible;
            }
            if(!visible)continue;

            ImVec2 vertices[4];
            for(int i=0;i<4;++i) vertices[i]={points[i][0],points[i][1]};

            if(showZoneOverlay && (flags&29))
            {
                const auto color=(flags&5)==5?CombinedColor:((flags&4)?BlockColor:((flags&1)?SafeColor:
                    ((flags&8)?IM_COL32(185,110,225,60):IM_COL32(55,155,255,60))));
                draw->AddConvexPolyFilled(vertices,4,color);
            }

            if(!brushCell) continue;

            if(materialMode)
            {
                if(showMaterialPreview_ && materialTexture != 0 && brush_.Mode != TerrainBrushMode::EraseBlend)
                {
                    draw->AddImageQuad(static_cast<ImTextureID>(materialTexture),
                        vertices[0], vertices[1], vertices[2], vertices[3],
                        {0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}, previewTint);
                }
                else
                {
                    const auto fill = brush_.Mode == TerrainBrushMode::EraseBlend
                        ? IM_COL32(90,160,255,38)
                        : IM_COL32(90,220,190,32);
                    draw->AddConvexPolyFilled(vertices,4,fill);
                }
            }
            else
            {
                const auto fill=brush_.Mode==TerrainBrushMode::Lower
                    ? IM_COL32(255,95,75,34) : IM_COL32(85,230,210,30);
                draw->AddConvexPolyFilled(vertices,4,fill);
            }

            if(!insideBrush(x,y-1)) draw->AddLine(vertices[0],vertices[1],BrushColor,2.0f);
            if(!insideBrush(x+1,y)) draw->AddLine(vertices[1],vertices[2],BrushColor,2.0f);
            if(!insideBrush(x,y+1)) draw->AddLine(vertices[2],vertices[3],BrushColor,2.0f);
            if(!insideBrush(x-1,y)) draw->AddLine(vertices[3],vertices[0],BrushColor,2.0f);

            if(x==cx && y==cy)
            {
                const ImVec2 mid{
                    (vertices[0].x+vertices[1].x+vertices[2].x+vertices[3].x)*0.25f,
                    (vertices[0].y+vertices[1].y+vertices[2].y+vertices[3].y)*0.25f};
                draw->AddCircleFilled(mid,4.0f,BrushColor,16);
                draw->AddCircle(mid,8.0f,IM_COL32(255,255,255,220),20,1.5f);
            }
        }

    if (brush_.Mode == TerrainBrushMode::Ramp && rampAnchor_)
    {
        const int ax = (*rampAnchor_)[0], ay = (*rampAnchor_)[1];
        std::array<float,2> startScreen{}, endScreen{};
        if (camera.Project({(ax + 0.5f) * TileSize, (ay + 0.5f) * TileSize,
                document.Height(ax, ay) + OverlayLift + 4.0f}, startScreen)
            && camera.Project({hit_[0], hit_[1], hit_[2] + OverlayLift + 4.0f}, endScreen))
        {
            draw->AddLine({startScreen[0], startScreen[1]}, {endScreen[0], endScreen[1]},
                IM_COL32(255, 210, 70, 245), 3.0f);
            draw->AddCircleFilled({startScreen[0], startScreen[1]}, 6.0f, IM_COL32(255, 210, 70, 245), 18);
        }
    }
}
}
