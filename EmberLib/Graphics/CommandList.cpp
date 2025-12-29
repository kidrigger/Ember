#include "CommandList.hpp"

#include <algorithm>

#include "Util/HelperUtils.hpp"

HRESULT Ember::CommandList::Reset( ComPtr<ID3D12CommandAllocator> allocator )
{
  auto hresult = m_CommandList->Reset( allocator.Get(), nullptr );
  if ( FAILED( hresult ) ) return hresult;

  m_CommandAllocator = std::move( allocator );
  return hresult;
}

HRESULT Ember::CommandList::Close()
{
  return m_CommandList->Close();
}

ID3D12GraphicsCommandList6* Ember::CommandList::Get() const noexcept
{
  return m_CommandList.Get();
}

//ID3D12GraphicsCommandList6* Ember::CommandList::operator->() const noexcept
//{
//  return m_CommandList.Get();
//}

std::pair<ComPtr<ID3D12GraphicsCommandList6>, ComPtr<ID3D12CommandAllocator>> Ember::CommandList::Release() noexcept
{
  return { std::move( m_CommandList ), std::move( m_CommandAllocator ) };
}

void Ember::CommandList::SetDescriptorHeaps( std::span<ID3D12DescriptorHeap* const> heaps ) const
{
  m_CommandList->SetDescriptorHeaps( ( UINT )heaps.size(), heaps.data() );
}

void Ember::CommandList::SetComputeRootSignature( ID3D12RootSignature* root_signature ) const
{
  m_CommandList->SetComputeRootSignature( root_signature );
}

void Ember::CommandList::SetPipelineState( ID3D12PipelineState* pipeline_state ) const
{
  m_CommandList->SetPipelineState( pipeline_state );
}

void Ember::CommandList::Dispatch( ThreadGroupCount tgc ) const
{
  ASSERT( tgc.X > 0 and tgc.Y > 0 and tgc.Z > 0 );
  m_CommandList->Dispatch( tgc.X, tgc.Y, tgc.Z );
}

void Ember::CommandList::ResourceBarrier( CD3DX12_RESOURCE_BARRIER const& barrier ) const
{
  m_CommandList->ResourceBarrier( 1u, &barrier );
}

void Ember::CommandList::ResourceBarrier( std::span<CD3DX12_RESOURCE_BARRIER> const& barriers ) const
{
  m_CommandList->ResourceBarrier( ( UINT )barriers.size(), barriers.data() );
}
