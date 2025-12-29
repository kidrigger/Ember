#pragma once

#include <array>

#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "Scene.hpp"
#include "fg/Blackboard.hpp"
#include "fg/FrameGraphResource.hpp"


class FrameGraph;
namespace Ember
{
class RenderDevice;
}

namespace Ember::RenderPass
{

struct GBuffer
{
  enum GBufferIndex
  {
    kPosition     = 0,
    kAlbedo       = 1,
    kNormal       = 2,
    kORM          = 3,
    kEmissive     = 4,
    kGBufferCount = 5,
  };

  constexpr static char const* kGBufferNames[kGBufferCount] = {
    "GBuffer Position", "GBuffer Albedo", "GBuffer Normal", "GBuffer ORM", "GBuffer Emissive",
  };

  // TODO: Use more compact formats if possible.
  // R32G32B32A32_FLOAT is overkill for position, but required for the shadow.
  // More quantization?
  DXGI_FORMAT constexpr static kGBufferFormats[kGBufferCount] = {
    DXGI_FORMAT_R32G32B32A32_FLOAT,  // Position
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // Albedo
    DXGI_FORMAT_R16G16_UNORM,        // Normal
    DXGI_FORMAT_R8G8B8A8_UNORM,      // ORM
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // Emissive
  };

  struct Data
  {
    std::array<FrameGraphResource, kGBufferCount> GBuffer;
    FrameGraphResource                            DepthStencil;
  };

  // Specifics

  // PBR Pipeline
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create( GBuffer* out, RenderDevice* render_device, DXGI_FORMAT depth_format );

  Data Execute( FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth_stencil );
  Data operator()( FrameGraph* frame_graph, FrameGraphBlackboard const& bb, FrameGraphResource depth_stencil );
};

} // namespace Ember::RenderPass
