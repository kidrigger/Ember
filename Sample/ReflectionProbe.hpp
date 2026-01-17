#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Float16.hpp>
#include <Util/Runtime.hpp>

#include "fg/FrameGraphResource.hpp"

class FrameGraph;
class FrameGraphBlackboard;

namespace Ember
{
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
    Float16 PositionX;
    Float16 PositionY;
    Float16 PositionZ;
    Float16 CaptureRadius;
  };

private:
  const static uint32_t       kSide               = 64;
  const static DXGI_FORMAT    kRenderTargetFormat = DXGI_FORMAT_R11G11B10_FLOAT;
  const static DXGI_FORMAT    kDepthFormat        = DXGI_FORMAT_D16_UNORM;

  ComPtr<ID3D12PipelineState> m_Pipeline;
  ComPtr<ID3D12RootSignature> m_RootSignature;

public:
  Probe ProbeInfo;

  ReflectionProbe() = default;
  ReflectionProbe(
      ComPtr<ID3D12PipelineState> pipeline, ComPtr<ID3D12RootSignature> root_signature, Probe const& probe_info );

  static bool Create( ReflectionProbe* out, RenderDevice* render_device, DirectX::XMFLOAT3 position, float radius );

  FrameGraphResource Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard, TextureLoader* loader ) const;
  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& blackboard, TextureLoader* loader ) const;
};
} // namespace Ember::Proto
