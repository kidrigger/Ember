#pragma once

#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>
#include "GBufferPass.hpp"

namespace Ember::RenderPass
{

struct OmniLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;

  static bool                 Create(
                      OmniLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  FrameGraphResource Execute(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, GBuffer::Data const& gbuffer ) const;

  FrameGraphResource operator()(
      FrameGraph* frame_graph, FrameGraphBlackboard const& bb, GBuffer::Data const& gbuffer ) const;
};

struct SpotLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  static bool                 Create(
                      SpotLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format, DXGI_FORMAT depth_format );

  FrameGraphResource Execute(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      GBuffer::Data const&        gbuffer,
      FrameGraphResource          render_target ) const;

  FrameGraphResource operator()(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      GBuffer::Data const&        gbuffer,
      FrameGraphResource          render_target ) const;
};

struct ScreenSpaceLightDeferred
{
  ComPtr<ID3D12RootSignature> RootSignature;
  ComPtr<ID3D12PipelineState> Pipeline;
  DXGI_FORMAT                 RenderTargetFormat{ DXGI_FORMAT_UNKNOWN };

  static bool        Create( ScreenSpaceLightDeferred* out, RenderDevice* render_device, DXGI_FORMAT rt_format );

  FrameGraphResource Execute(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      GBuffer::Data const&        gbuffer,
      FrameGraphResource          render_target ) const;

  FrameGraphResource operator()(
      FrameGraph*                 frame_graph,
      FrameGraphBlackboard const& bb,
      GBuffer::Data const&        gbuffer,
      FrameGraphResource          render_target ) const;
};

} // namespace Ember::RenderPass
