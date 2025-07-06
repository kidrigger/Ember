#include "DepthBuffer.hpp"

#include "Util/HelperUtils.hpp"

Ember::DepthBuffer::DepthBuffer( ComPtr<ID3D12Resource>&& buffer, ComPtr<D3D12MA::Allocation>&& allocation )
  : m_Buffer{ std::move( buffer ) }, m_Allocation{ std::move( allocation ) }
{}

ID3D12Resource* Ember::DepthBuffer::GetBuffer() const
{
  ASSERT( m_Buffer );
  return m_Buffer.Get();
}
