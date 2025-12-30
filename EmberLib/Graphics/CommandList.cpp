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

// ID3D12GraphicsCommandList6* Ember::CommandList::operator->() const noexcept
//{
//   return m_CommandList.Get();
// }

Ember::CommandList::Content Ember::CommandList::Release() noexcept
{
  return { std::move( m_CommandList ), std::move( m_CommandAllocator ), std::move( m_RenderTargetManager ) };
}

void Ember::CommandList::CopyResource( ID3D12Resource* dest, ID3D12Resource* source )
{
  m_CommandList->CopyResource( dest, source );
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
  ASSERT( not barriers.empty() );
  m_CommandList->ResourceBarrier( ( UINT )barriers.size(), barriers.data() );
}

void Ember::CommandList::SetGraphicsRootSignature( ID3D12RootSignature* root_signature ) const
{
  m_CommandList->SetGraphicsRootSignature( root_signature );
}

void Ember::CommandList::IASetPrimitiveTopology( D3D12_PRIMITIVE_TOPOLOGY topology ) const
{
  m_CommandList->IASetPrimitiveTopology( topology );
}

void Ember::CommandList::DispatchMesh( ThreadGroupCount tgc ) const
{
  ASSERT( tgc.X > 0 and tgc.Y > 0 and tgc.Z > 0 );
  m_CommandList->DispatchMesh( tgc.X, tgc.Y, tgc.Z );
}

void Ember::CommandList::DrawInstanced(
    uint32_t const vertex_count_per_instance,
    uint32_t const instance_count,
    uint32_t const start_vertex_location,
    uint32_t const start_instance_location ) const
{
  m_CommandList->DrawInstanced(
      vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location );
}

void Ember::CommandList::SetGraphicsRootConstantBufferView(
    uint32_t root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS const buffer_location )
{
  m_CommandList->SetGraphicsRootConstantBufferView( root_parameter_index, buffer_location );
}

void Ember::CommandList::SetGraphicsRootConstant(
    uint32_t root_parameter_index, uint32_t value, uint32_t index_offset ) const
{
  m_CommandList->SetGraphicsRoot32BitConstant( root_parameter_index, value, index_offset );
}

void Ember::CommandList::RSSetScissorViewport( uint32_t width, uint32_t height ) const
{
  m_RenderTargetManager->RSSetScissorViewport( m_CommandList.Get(), width, height );
}

void Ember::CommandList::ClearRenderTargetView( ID3D12Resource* render_target, float const color[] ) const
{
  m_RenderTargetManager->ClearRenderTargetView( m_CommandList.Get(), render_target, color );
}

void Ember::CommandList::ClearRenderTargetView( Texture const& render_target, float const color[] ) const
{
  m_RenderTargetManager->ClearRenderTargetView( m_CommandList.Get(), render_target, color );
}

void Ember::CommandList::ClearRenderTargetViews(
    uint32_t count, ID3D12Resource** render_target, float const color[] ) const
{
  m_RenderTargetManager->ClearRenderTargetViews( m_CommandList.Get(), count, render_target, color );
}

void Ember::CommandList::ClearDepthStencilView(
    ID3D12Resource* depth_stencil, D3D12_CLEAR_FLAGS flags, float depth, uint8_t stencil ) const
{
  m_RenderTargetManager->ClearDepthStencilView( m_CommandList.Get(), depth_stencil, flags, depth, stencil );
}

void Ember::CommandList::OMSetRenderTargets(
    uint32_t                             count,
    ID3D12Resource**                     render_targets,
    D3D12_RENDER_TARGET_VIEW_DESC const* rtv_desc,
    ID3D12Resource*                      depth_stencil,
    D3D12_DEPTH_STENCIL_VIEW_DESC const* dsv_desc ) const
{
  m_RenderTargetManager->OMSetRenderTargets(
      m_CommandList.Get(), count, render_targets, rtv_desc, depth_stencil, dsv_desc );
}

void Ember::CommandList::OMSetRenderTargets(
    uint32_t count, Texture const* render_targets, Texture const* depth_stencil ) const
{
  m_RenderTargetManager->OMSetRenderTargets( m_CommandList.Get(), count, render_targets, depth_stencil );
}

void Ember::CommandList::DiscardResource( ID3D12Resource* resource, D3D12_DISCARD_REGION const* discard_region ) const
{
  m_CommandList->DiscardResource( resource, discard_region );
}
