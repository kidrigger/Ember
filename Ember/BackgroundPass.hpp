#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct Background
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> SkyboxPipeline;
  ComPtr<ID3D12PipelineState> AtmospherePipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  static bool                 Create( Background* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

} // namespace Ember::RenderPass
