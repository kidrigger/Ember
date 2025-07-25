#pragma once

#include "BindlessHandle.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class Texture
{
  ComPtr<ID3D12Resource>      m_Texture;
  ComPtr<D3D12MA::Allocation> m_Allocation;
  SRVHandle                   m_SRVHandle;

public:
  Texture() = default;

  Texture( ComPtr<ID3D12Resource> texture, ComPtr<D3D12MA::Allocation> allocation, SRVHandle const handle )
    : m_Texture{ std::move( texture ) }, m_Allocation{ std::move( allocation ) }, m_SRVHandle{ handle }
  {}

  ID3D12Resource* GetTexture() const
  {
    return m_Texture.Get();
  }

  SRVHandle GetSRVHandle() const
  {
    return m_SRVHandle;
  }
};

} // namespace Ember
