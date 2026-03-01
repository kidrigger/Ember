#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "fg/Blackboard.hpp"
#include "fg/FrameGraphResource.hpp"

namespace Ember
{
class RenderDevice;
}
class FrameGraph;

namespace Ember::RenderPass
{

struct ScreenSpaceAmbientOcclusionBlur
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create( ScreenSpaceAmbientOcclusionBlur* out, RenderDevice* render_device );

  FrameGraphResource          Execute(
               FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource ssao_texture ) const;

  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource ssao_texture ) const;
};

} // namespace Ember::RenderPass
