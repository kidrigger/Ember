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
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create( Background* out, RenderDevice* render_device );
};

} // namespace Ember::RenderPass
