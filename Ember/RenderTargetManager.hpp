#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class Texture;
class RenderDevice;

class RenderTargetManager
{
  ComPtr<ID3D12Device>         m_D3D12Device;
  ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
  ComPtr<ID3D12DescriptorHeap> m_DSVDescriptorHeap;
  uint32_t                     m_RTVDescriptorSize;
  uint32_t                     m_DSVDescriptorSize;

public:
  static void Create( RenderTargetManager* render_target_manager, RenderDevice* device );

  RenderTargetManager() = default;
  RenderTargetManager(
      ComPtr<ID3D12Device>         d3d12_device,
      ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap,
      uint32_t                     rtv_descriptor_size,
      ComPtr<ID3D12DescriptorHeap> dsv_descriptor_heap,
      uint32_t                     dsv_descriptor_size );

  void ClearRenderTargetView(
      ID3D12GraphicsCommandList* command_list, Texture const& render_target, float const color[] ) const;
  void ClearDepthStencilView(
      ID3D12GraphicsCommandList* command_list,
      Texture const&             depth_stencil,
      D3D12_CLEAR_FLAGS          flags,
      float                      depth,
      uint8_t                    stencil ) const;

  void OMSetRenderTargets(
      ID3D12GraphicsCommandList* command_list,
      uint8_t                    count,
      Texture const*             render_targets,
      Texture const*             depth_stencil ) const;

  void OMSetRenderTargets(
      ID3D12GraphicsCommandList*           command_list,
      uint8_t                              count,
      Texture const*                       render_targets,
      D3D12_RENDER_TARGET_VIEW_DESC const* rtv_desc,
      Texture const*                       depth_stencil,
      D3D12_DEPTH_STENCIL_VIEW_DESC const* dsv_desc ) const;
};

} // namespace Ember
