#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "fg/FrameGraphResource.hpp"

class FrameGraphBlackboard;
class FrameGraph;

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{
struct RenderDepthData;

struct Skybox
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> SkyboxPipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  struct Data
  {
    FrameGraphResource RenderTarget;
    FrameGraphResource DepthStencil;
    FrameGraphResource SkyViewLUT;
  };

  static bool Create( Skybox* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  FrameGraphResource Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const;

  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& depth ) const;
};

struct AtmosphereSkybox
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> AtmospherePipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  struct Data
  {
    FrameGraphResource RenderTarget;
    FrameGraphResource DepthStencil;
    FrameGraphResource SkyViewLUT;
  };

  static bool Create(
      AtmosphereSkybox* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  FrameGraphResource Execute(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      RenderDepthData const&      render_depth,
      FrameGraphResource          sky_view_lut ) const;

  FrameGraphResource operator()(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      RenderDepthData const&      depth,
      FrameGraphResource          sky_view_lut ) const;
};

} // namespace Ember::RenderPass
