#pragma once

#include <cstdint>

#include "Atmosphere.hpp"
#include "DepthPrePass.hpp"
#include "ForwardPass.hpp"
#include "GBufferPass.hpp"
#include "LightingPass.hpp"
#include "ReflectionProbe.hpp"
#include "SSAOBlurPass.hpp"
#include "SSAOPass.hpp"
#include "SkyboxPass.hpp"
#include "TransparencyPass.hpp"
#include "fg/FrameGraphResource.hpp"

class FrameGraph;
class FrameGraphBlackboard;

namespace Ember
{
class MipMapGenerator;
class RenderDevice;

class RenderPipeline
{
public:
  struct Settings
  {
    bool UseDeferredRendering;
    bool UseProbes;
    bool UseSSAO;
    bool UseProceduralAtmosphericSky;
  };

private:
  DXGI_FORMAT                                 m_DepthFormat{ DXGI_FORMAT_UNKNOWN };
  uint32_t                                    m_SunLightIndex{ UINT32_MAX };

  RenderPass::DepthPrePass                    m_DrawPrePass;
  RenderPass::OpaqueForward                   m_RenderOpaqueMeshes;

  RenderPass::GBuffer                         m_UpdateGBuffer;
  RenderPass::OmniLightDeferred               m_RenderOmniLights;
  RenderPass::SpotLightDeferred               m_RenderSpotLights;
  RenderPass::ScreenSpaceLightDeferred        m_RenderScreenSpaceLighting;
  RenderPass::ScreenSpaceAmbientOcclusion     m_RenderSSAO;
  RenderPass::ScreenSpaceAmbientOcclusionBlur m_RenderSSAOBlur;

  RenderPass::MaskedForward                   m_RenderMaskedMeshes;
  RenderPass::TransparencyForward             m_RenderTransparentMeshes;

  RenderPass::Atmosphere                      m_UpdateAtmosphericSky;
  RenderPass::Skybox                          m_RenderBackground;
  Proto::ReflectionProbe                      m_Probe;

public:
  RenderPipeline() = default;

  RenderPipeline(
      DXGI_FORMAT                                 depth_format,
      RenderPass::DepthPrePass                    draw_pre_pass,
      RenderPass::OpaqueForward                   render_opaque_meshes,
      RenderPass::GBuffer                         update_g_buffer,
      RenderPass::OmniLightDeferred               render_omni_lights,
      RenderPass::SpotLightDeferred               render_spot_lights,
      RenderPass::ScreenSpaceLightDeferred        render_screen_space_lighting,
      RenderPass::ScreenSpaceAmbientOcclusion     render_ssao,
      RenderPass::ScreenSpaceAmbientOcclusionBlur render_ssao_blur,
      RenderPass::MaskedForward                   render_masked_meshes,
      RenderPass::TransparencyForward             render_transparent_meshes,
      RenderPass::Atmosphere                      update_atmospheric_sky,
      RenderPass::Skybox                          render_background,
      Proto::ReflectionProbe                      probe );

  static bool Create(
      RenderPipeline* out, RenderDevice* render_device, MipMapGenerator* mip_map_generator, DXGI_FORMAT depth_format );
  void               SetSunIndex( uint32_t sun_light_index );

  FrameGraphResource Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t frame_idx, Settings const& settings );
};

} // namespace Ember
