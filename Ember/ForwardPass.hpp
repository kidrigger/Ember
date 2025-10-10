#pragma once

#include "DeviceHandle.hpp"
#include "LightManager.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"
#include "fg/FrameGraphResource.hpp"

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct OpaqueForward
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> OpaquePipeline;
  ComPtr<ID3D12PipelineState> AlphaTestedPipeline;
  DXGI_FORMAT                 RenderTargetFormat;

  OpaqueForward() = default;
  static bool Create( OpaqueForward* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

struct RTVData
{
  FrameGraphResource RenderTarget;
  FrameGraphResource DepthStencil;
};

} // namespace Ember::RenderPass
