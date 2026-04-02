#include "RenderPipeline.hpp"

#include <Graphics/RenderDevice.hpp>

#include "FrameGraphHelper.hpp"
#include "RenderPassCommon.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraph.hpp"

Ember::RenderPipeline::RenderPipeline(
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
    RenderPass::AtmosphereSkybox                render_atmos_background )
  : m_DepthFormat{ depth_format }
  , m_DrawPrePass{ std::move( draw_pre_pass ) }
  , m_RenderOpaqueMeshes{ std::move( render_opaque_meshes ) }
  , m_UpdateGBuffer{ std::move( update_g_buffer ) }
  , m_RenderOmniLights{ std::move( render_omni_lights ) }
  , m_RenderSpotLights{ std::move( render_spot_lights ) }
  , m_RenderScreenSpaceLighting{ std::move( render_screen_space_lighting ) }
  , m_RenderSSAO{ std::move( render_ssao ) }
  , m_RenderSSAOBlur{ std::move( render_ssao_blur ) }
  , m_RenderMaskedMeshes{ std::move( render_masked_meshes ) }
  , m_RenderTransparentMeshes{ std::move( render_transparent_meshes ) }
  , m_UpdateAtmosphericSky{ std::move( update_atmospheric_sky ) }
  , m_RenderBackground{ std::move( render_background ) }
  , m_RenderAtmosphereBackground{ std::move( render_atmos_background ) }
{}

bool Ember::RenderPipeline::Create( RenderPipeline* out, RenderDevice* render_device, DXGI_FORMAT const depth_format )
{
  auto const                                  swapchain_format = render_device->GetSwapchainFormat();

  RenderPass::DepthPrePass                    draw_pre_pass;
  RenderPass::OpaqueForward                   render_opaque_meshes;

  RenderPass::GBuffer                         update_g_buffer;
  RenderPass::OmniLightDeferred               render_omni_lights;
  RenderPass::SpotLightDeferred               render_spot_lights;
  RenderPass::ScreenSpaceLightDeferred        render_screen_space_lighting;
  RenderPass::ScreenSpaceAmbientOcclusion     render_ssao;
  RenderPass::ScreenSpaceAmbientOcclusionBlur render_ssao_blur;

  RenderPass::MaskedForward                   render_masked_meshes;
  RenderPass::TransparencyForward             render_transparent_meshes;

  RenderPass::Atmosphere                      update_atmospheric_sky;
  RenderPass::Skybox                          render_background;
  RenderPass::AtmosphereSkybox                render_atmosphere_background;

  ENSURE( RenderPass::DepthPrePass::Create( &draw_pre_pass, render_device, depth_format ) );
  ENSURE( RenderPass::OpaqueForward::Create(
      &render_opaque_meshes,
      {
          .RenderDevice          = render_device,
          .RenderTargetFormat    = DirectX::MakeSRGB( swapchain_format ),
          .DepthStencilFormat    = depth_format,
          .DependsOnDepthPrePass = true,
      } ) );

  ENSURE( RenderPass::GBuffer::Create( &update_g_buffer, render_device, depth_format ) );
  ENSURE( RenderPass::OmniLightDeferred::Create(
      &render_omni_lights, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );
  ENSURE( RenderPass::SpotLightDeferred::Create(
      &render_spot_lights, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );
  ENSURE( RenderPass::ScreenSpaceLightDeferred::Create(
      &render_screen_space_lighting, render_device, DirectX::MakeSRGB( swapchain_format ) ) );
  ENSURE( RenderPass::ScreenSpaceAmbientOcclusion::Create( &render_ssao, render_device ) );
  ENSURE( RenderPass::ScreenSpaceAmbientOcclusionBlur::Create( &render_ssao_blur, render_device ) );

  ENSURE( RenderPass::MaskedForward::Create(
      &render_masked_meshes, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );
  ENSURE( RenderPass::TransparencyForward::Create(
      &render_transparent_meshes, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );
  ENSURE( RenderPass::Skybox::Create(
      &render_background, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );
  ENSURE( RenderPass::AtmosphereSkybox::Create(
      &render_atmosphere_background, render_device, DirectX::MakeSRGB( swapchain_format ), depth_format ) );

  ENSURE( RenderPass::Atmosphere::Create( &update_atmospheric_sky, render_device ) );

  new ( out ) RenderPipeline{
    depth_format,
    std::move( draw_pre_pass ),
    std::move( render_opaque_meshes ),
    std::move( update_g_buffer ),
    std::move( render_omni_lights ),
    std::move( render_spot_lights ),
    std::move( render_screen_space_lighting ),
    std::move( render_ssao ),
    std::move( render_ssao_blur ),
    std::move( render_masked_meshes ),
    std::move( render_transparent_meshes ),
    std::move( update_atmospheric_sky ),
    std::move( render_background ),
    std::move( render_atmosphere_background ),
  };
  out->m_DepthFormat = depth_format;

  return true;
}

void Ember::RenderPipeline::SetSunIndex( uint32_t const sun_light_index )
{
  m_SunLightIndex = sun_light_index;
}

FrameGraphResource Ember::RenderPipeline::Execute(
    FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t const frame_idx, Settings const& settings )
{
  m_UpdateAtmosphericSky.SetSun( m_SunLightIndex );

  auto const atmosphere      = m_UpdateAtmosphericSky( frame_graph, blackboard, frame_idx );
  auto const depth_buffer    = m_DrawPrePass( frame_graph, *blackboard );

  auto const ssao_pass       = m_RenderSSAO( frame_graph, *blackboard, depth_buffer );
  auto const ssao_blur_pass  = m_RenderSSAOBlur( frame_graph, *blackboard, ssao_pass, depth_buffer );
  auto const ssao_resource   = settings.UseSSAO ? ssao_blur_pass : FrameGraphResource{};

  auto const opaque_pass_fwd = m_RenderOpaqueMeshes( frame_graph, *blackboard, depth_buffer, ssao_resource );

  auto const gbuffer         = m_UpdateGBuffer( frame_graph, *blackboard, depth_buffer );
  auto const omni_pass_rt    = m_RenderOmniLights( frame_graph, *blackboard, gbuffer, ssao_resource );
  auto const spot_pass_rt    = m_RenderSpotLights( frame_graph, *blackboard, gbuffer, omni_pass_rt, ssao_resource );
  auto const opaque_pass_dfr =
      m_RenderScreenSpaceLighting( frame_graph, *blackboard, gbuffer, spot_pass_rt, ssao_resource );

  auto const opaque_pass = RenderPass::RenderDepthData{
    .RenderTarget = settings.UseDeferredRendering ? opaque_pass_dfr : opaque_pass_fwd,
    .DepthStencil = depth_buffer,
  };

  auto const alpha_tested      = m_RenderMaskedMeshes( frame_graph, *blackboard, opaque_pass, ssao_resource );
  auto const transparency_pass = m_RenderTransparentMeshes( frame_graph, *blackboard, alpha_tested, ssao_resource );

  auto const skybox_pass =
      settings.UseProceduralAtmosphericSky
          ? m_RenderAtmosphereBackground( frame_graph, *blackboard, transparency_pass, atmosphere.SkyViewLUT )
          : m_RenderBackground( frame_graph, *blackboard, transparency_pass );

  return settings.UseSkybox ? skybox_pass : transparency_pass.RenderTarget;
}
