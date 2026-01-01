#pragma once

#include <span>
#include "RenderTargetManager.hpp"
#include "ResourceBinder.hpp"
#include "Util/DataUtil.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class Texture;

class CommandList
{
  ComPtr<ID3D12GraphicsCommandList6>   m_CommandList;
  ComPtr<ID3D12CommandAllocator>       m_CommandAllocator;
  std::unique_ptr<RenderTargetManager> m_RenderTargetManager;
  std::unique_ptr<ResourceBinder>      m_Binder;

public:
  CommandList() = default;
  CommandList(
      ComPtr<ID3D12GraphicsCommandList6>   command_list,
      ComPtr<ID3D12CommandAllocator>       command_allocator,
      std::unique_ptr<RenderTargetManager> render_target_manager,
      std::unique_ptr<ResourceBinder>      binder );

  struct ThreadGroupCount
  {
    uint32_t X = 1;
    uint32_t Y = 1;
    uint32_t Z = 1;
  };

  void CopyResource( ID3D12Resource* dest, ID3D12Resource* source );

  void ResourceBarrier( CD3DX12_RESOURCE_BARRIER const& barriers ) const;
  void ResourceBarrier( std::span<CD3DX12_RESOURCE_BARRIER> const& barriers ) const;

  void SetDescriptorHeaps( std::span<ID3D12DescriptorHeap* const> heaps ) const;
  void SetComputeRootSignature( ID3D12RootSignature* root_signature ) const;
  void SetPipelineState( ID3D12PipelineState* pipeline_state ) const;
  void Dispatch( ThreadGroupCount tgc ) const;
  void SetComputeRootConstants( uint32_t root_parameter_index, auto const& value, uint32_t byte_offset = 0 ) const
  { // TODO: Move to autocasting process.
    ASSERT( byte_offset % 4 == 0 );
    m_CommandList->SetComputeRoot32BitConstants( root_parameter_index, sizeof( value ) / 4, &value, byte_offset / 4 );
  }

  void BindComputeResources( uint32_t root_parameter_index, BindableStructure auto const& bindable_structure ) const
  {
    auto data = bindable_structure.Bind( m_Binder.get() );
    m_CommandList->SetComputeRoot32BitConstants( root_parameter_index, sizeof( data ) / 4, &data, 0 );
  }

  void SetGraphicsRootSignature( ID3D12RootSignature* root_signature ) const;
  void IASetPrimitiveTopology( D3D12_PRIMITIVE_TOPOLOGY topology ) const;
  void DispatchMesh( ThreadGroupCount tgc ) const;
  void DrawInstanced(
      uint32_t vertex_count_per_instance,
      uint32_t instance_count,
      uint32_t start_vertex_location,
      uint32_t start_instance_location ) const;
  void SetGraphicsRootConstantBufferView( uint32_t root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location );
  void SetGraphicsRootConstant( uint32_t root_parameter_index, uint32_t value, uint32_t index_offset = 0 ) const;
  void SetGraphicsRootConstants(
      uint32_t root_parameter_index, std::ranges::range auto const& value, uint32_t byte_offset = 0 ) const
  {
    // TODO: Move to autocasting process.
    ASSERT( byte_offset % 4 == 0 );
    m_CommandList->SetGraphicsRoot32BitConstants(
        root_parameter_index, ByteSizeOf( value ) / 4, DataOf( value ), byte_offset / 4 );
  }

  void SetGraphicsRootConstants(
      uint32_t root_parameter_index, IsUnitObject auto const& value, uint32_t byte_offset = 0 ) const
  {
    // TODO: Move to autocasting process.
    ASSERT( byte_offset % 4 == 0 );
    m_CommandList->SetGraphicsRoot32BitConstants( root_parameter_index, sizeof( value ) / 4, &value, byte_offset / 4 );
  }

  void RSSetScissorViewport( uint32_t width, uint32_t height ) const;
  void ClearRenderTargetView( ID3D12Resource* render_target, float const color[] ) const;
  void ClearRenderTargetView( Texture const& render_target, float const color[] ) const;
  void ClearRenderTargetViews( uint32_t count, ID3D12Resource** render_target, float const color[] ) const;

  void ClearDepthStencilView(
      ID3D12Resource* depth_stencil, D3D12_CLEAR_FLAGS flags, float depth, uint8_t stencil ) const;

  void OMSetRenderTargets(
      uint32_t                             count,
      ID3D12Resource**                     render_targets,
      D3D12_RENDER_TARGET_VIEW_DESC const* rtv_desc,
      ID3D12Resource*                      depth_stencil,
      D3D12_DEPTH_STENCIL_VIEW_DESC const* dsv_desc ) const;

  void    OMSetRenderTargets( uint32_t count, Texture const* render_targets, Texture const* depth_stencil ) const;
  void    DiscardResource( ID3D12Resource* resource, D3D12_DISCARD_REGION const* discard_region = nullptr ) const;

  HRESULT Reset( ComPtr<ID3D12CommandAllocator> allocator );
  HRESULT Close();

  ID3D12GraphicsCommandList6* Get() const noexcept;

  using Content = std::tuple<
      ComPtr<ID3D12GraphicsCommandList6>,
      ComPtr<ID3D12CommandAllocator>,
      std::unique_ptr<RenderTargetManager>,
      std::unique_ptr<ResourceBinder>>;
  [[nodiscard]] Content Release() noexcept;
}; // namespace Ember

} // namespace Ember
