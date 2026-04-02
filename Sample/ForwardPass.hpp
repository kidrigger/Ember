#pragma once

#include <Graphics/DeviceHandle.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "LightManager.hpp"
#include "fg/FrameGraphResource.hpp"


class FrameGraphBlackboard;
class FrameGraph;

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct RenderDepthData
{
  FrameGraphResource RenderTarget;
  FrameGraphResource DepthStencil;
};

struct OpaqueForward
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  struct Desc
  {
    RenderDevice* RenderDevice;
    DXGI_FORMAT   RenderTargetFormat;
    DXGI_FORMAT   DepthStencilFormat;
    bool          DependsOnDepthPrePass = false;
  };

  static bool        Create( OpaqueForward* out, Desc const& desc );
  FrameGraphResource Execute( FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth ) const;

  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth ) const;
};

} // namespace Ember::RenderPass
