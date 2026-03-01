#ifndef DEBUG_CONFIG_HLSLI_
#define DEBUG_CONFIG_HLSLI_

#ifndef STRIP_DEBUG_CONFIG

enum VisMode
{
  kRender        = 0,
  kMeshlet       = 1,
  kWorldPosition = 2,
  kAlbedo        = 3,
  kNormal        = 4,
  kORM           = 5,
  kEmissive      = 6,
  kLightingOnly  = 7,
  kAO            = 8,
};

struct DebugConfig
{
  uint    ShowDebugUI;
  uint    ShowWireframe;
  VisMode VisualizationMode;
  uint    HideSkybox;
  uint    RemoveDiffuseContrib;
  uint    RemoveSpecularContrib;
  uint    DisableMeshletFrustumCulling;

  bool    IsLitVisMode()
  {
    return VisualizationMode == kRender || VisualizationMode == kLightingOnly;
  }
};
#endif

#endif
