#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class DepthBuffer
{
  ComPtr<ID3D12Resource>      m_Buffer;
  ComPtr<D3D12MA::Allocation> m_Allocation;

public:
  DepthBuffer() = default;
  DepthBuffer( ComPtr<ID3D12Resource>&& buffer, ComPtr<D3D12MA::Allocation>&& allocation );

  [[nodiscard]] ID3D12Resource* GetBuffer() const;
};

} // namespace Ember
