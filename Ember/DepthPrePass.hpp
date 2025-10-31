#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"


#include "fg/FrameGraphResource.hpp"


namespace Ember
{
class RenderDevice;
}
class FrameGraphBlackboard;
class FrameGraph;

namespace Ember::RenderPass
{

class DepthPrePass
{
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_OpaquePipeline;
  ComPtr<ID3D12PipelineState> m_AlphaTestedPipeline;

public:
  DepthPrePass() = default;
  DepthPrePass(
      ComPtr<ID3D12RootSignature> root_signature,
      ComPtr<ID3D12PipelineState> opaque_pipeline,
      ComPtr<ID3D12PipelineState> alpha_tested_pipeline );

  FrameGraphResource Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard, FrameGraphResource in_depth_texture );

  static bool Create( DepthPrePass* out, RenderDevice* render_device, DXGI_FORMAT const depth_format );
};

} // namespace Ember::RenderPass
