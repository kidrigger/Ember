#pragma once

#include "ForwardPass.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct TransparencyForward
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  static bool                 Create(
                      TransparencyForward* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  RenderDepthData Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const;

  RenderDepthData operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const;
};

struct AlphaTestedForward
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  static bool                 Create(
                      AlphaTestedForward* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  RenderDepthData Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const;

  RenderDepthData operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, RenderDepthData const& render_depth ) const;
};

} // namespace Ember::RenderPass
