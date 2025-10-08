#pragma once

#include "DeviceHandle.hpp"
#include "LightManager.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct Forward
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> OpaquePipeline;
  ComPtr<ID3D12PipelineState> AlphaTestedPipeline;
  ComPtr<ID3D12PipelineState> AlphaBlendedPipeline;

  struct PerFrameConstants
  {
    SRVHandle             MaterialsBuffer;
    CBVHandle             Camera;
    CBVHandle             ConfigBuffer;
    LightManager::GpuInfo LightInfo;
  };

  static bool Create( Forward* out, RenderDevice* render_device );
};

} // namespace Ember::RenderPass
