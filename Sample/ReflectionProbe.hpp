#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Float16.hpp>
#include <Util/Runtime.hpp>

#include "fg/FrameGraphResource.hpp"

class FrameGraph;
class FrameGraphBlackboard;

namespace Ember
{
class MipMapGenerator;
class RenderDevice;
class TextureLoader;
} // namespace Ember

namespace Ember::Proto
{

class ReflectionProbe
{
public:
  struct Probe
  {
    float PositionX;
    float PositionY;
    float PositionZ;
    float CaptureRadius;
  };

private:
  const static uint32_t       kSide               = 64;
  const static DXGI_FORMAT    kRenderTargetFormat = DXGI_FORMAT_R11G11B10_FLOAT;
  const static DXGI_FORMAT    kDepthFormat        = DXGI_FORMAT_D16_UNORM;

  MipMapGenerator*            m_MipMapGenerator;
  ComPtr<ID3D12PipelineState> m_Pipeline;
  ComPtr<ID3D12RootSignature> m_RootSignature;

public:
  Probe ProbeInfo;

  ReflectionProbe() = default;
  ReflectionProbe(
      MipMapGenerator*            mip_map_generator,
      ComPtr<ID3D12PipelineState> pipeline,
      ComPtr<ID3D12RootSignature> root_signature,
      Probe const&                probe_info );

  static bool Create(
      ReflectionProbe*  out,
      RenderDevice*     render_device,
      MipMapGenerator*  mip_map_generator,
      DirectX::XMFLOAT3 position,
      float             radius );

  FrameGraphResource Execute( FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const;
  FrameGraphResource operator()( FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard ) const;
};
} // namespace Ember::Proto
