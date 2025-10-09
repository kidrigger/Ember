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

  static bool                 Create( TransparencyForward* out, RenderDevice* render_device, DXGI_FORMAT rt_format );
};

} // namespace Ember::RenderPass
