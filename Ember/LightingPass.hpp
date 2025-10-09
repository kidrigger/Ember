#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct OmniLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create( OmniLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

struct SpotLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create( SpotLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

struct ScreenSpaceLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool Create( ScreenSpaceLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

} // namespace Ember::RenderPass
