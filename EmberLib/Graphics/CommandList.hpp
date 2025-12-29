#pragma once

#include <span>
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class CommandList
{
  ComPtr<ID3D12GraphicsCommandList6> m_CommandList;
  ComPtr<ID3D12CommandAllocator>     m_CommandAllocator;

public:
  CommandList() = default;
  CommandList( ComPtr<ID3D12GraphicsCommandList6> command_list, ComPtr<ID3D12CommandAllocator> command_allocator )
    : m_CommandList{ std::move( command_list ) }, m_CommandAllocator{ std::move( command_allocator ) }
  {}

  struct ThreadGroupCount
  {
    uint32_t X = 1;
    uint32_t Y = 1;
    uint32_t Z = 1;
  };

  void SetDescriptorHeaps( std::span<ID3D12DescriptorHeap* const> heaps ) const;
  void SetComputeRootSignature( ID3D12RootSignature* root_signature ) const;
  void SetPipelineState( ID3D12PipelineState* pipeline_state ) const;
  void Dispatch( ThreadGroupCount tgc ) const;
  void ResourceBarrier( CD3DX12_RESOURCE_BARRIER const& barriers ) const;
  void ResourceBarrier( std::span<CD3DX12_RESOURCE_BARRIER> const& barriers ) const;
  void SetComputeRootConstants( uint32_t root_parameter_index, auto const& value, uint32_t byte_offset = 0 ) const
  {
    ASSERT( byte_offset % 4 == 0 );
    m_CommandList->SetComputeRoot32BitConstants( root_parameter_index, sizeof( value ) / 4, &value, byte_offset / 4 );
  }

  HRESULT                     Reset( ComPtr<ID3D12CommandAllocator> allocator );
  HRESULT                     Close();

  ID3D12GraphicsCommandList6* Get() const noexcept;

  using Content = std::pair<ComPtr<ID3D12GraphicsCommandList6>, ComPtr<ID3D12CommandAllocator>>;
  [[nodiscard]] Content Release() noexcept;
};

} // namespace Ember
