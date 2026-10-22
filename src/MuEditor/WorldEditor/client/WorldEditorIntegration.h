#pragma once
#ifdef _EDITOR
namespace Silver::WorldEditor
{
bool IsStandaloneMode();
bool FreeLookEnabled();
bool ViewportCameraMouseEnabled();
bool ToggleFreeLookHotkey();
bool ResetCameraHotkey();
void BootstrapStandalone();
void UpdateClientEditor();
void DrawClientEditor(bool editorVisible);
bool CaptureGameInput();
void BeforeWorldChange();
void RenderClientPlacementPreview();
void ApplyRuntimeEffects();
}
#endif
