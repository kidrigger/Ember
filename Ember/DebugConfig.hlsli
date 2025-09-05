#ifndef DEBUG_CONFIG_HLSLI_
#define DEBUG_CONFIG_HLSLI_

// #define STRIP_DEBUG_CONFIG

#ifndef STRIP_DEBUG_CONFIG
struct DebugConfig
{
  uint ShowDebugUI;
  uint ShowWireframe;
  uint ShowLightOnly;
  uint HideSkybox;
  uint RemoveDiffuseContrib;
  uint RemoveSpecularContrib;
  uint DisableMeshletFrustumCulling;
  uint VisualizeMeshlets;
};
#endif

#endif
