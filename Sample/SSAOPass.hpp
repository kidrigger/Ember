#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "GBufferPass.hpp"

namespace Ember::RenderPass
{

struct ScreenSpaceAmbientOcclusion
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;
  Buffer                      Kernel;
  Buffer                      Rotation;

  static bool                 Create( ScreenSpaceAmbientOcclusion* out, RenderDevice* render_device );

  FrameGraphResource          Execute(
               FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth_buffer ) const;

  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth_buffer ) const;
};

} // namespace Ember::RenderPass
