#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

static void AllocateBufferImpl(
    D3D12MA::Allocator* allocator, uint32_t size, D3D12MA::Allocation** allocation, ID3D12Resource** resource );

class Buffer
{
public:
  enum class Type : uint8_t
  {
    kVertexBuffer   = 0,
    kIndexBuffer    = 1,
    kStorageBuffer  = 2,
    kConstantBuffer = 3,
  };

private:
  constexpr static uint32_t   kBufferTypeMask = 0b11;
  constexpr static uint32_t   kOffsetMask     = ~kBufferTypeMask;

  ComPtr<ID3D12Resource>      m_Buffer;
  ComPtr<D3D12MA::Allocation> m_Allocation;
  D3D12_GPU_VIRTUAL_ADDRESS   m_VirtualAddress{ 0 }; // Takes the Offset into account.
  uint32_t                    m_OffsetAndType{ 0 };
  uint32_t                    m_Size{ 0 };
  union
  {
    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
    D3D12_INDEX_BUFFER_VIEW  m_IndexBufferView;
  };

public:
  Buffer(
      ComPtr<ID3D12Resource>      buffer,
      ComPtr<D3D12MA::Allocation> allocation,
      Type                        type,
      uint32_t                    offset,
      uint32_t                    stride,
      uint32_t                    size );
  Buffer(
      ComPtr<ID3D12Resource>      buffer,
      ComPtr<D3D12MA::Allocation> allocation,
      Type                        type,
      uint32_t                    offset,
      DXGI_FORMAT                 format,
      uint32_t                    size );
  Buffer() = default;

  void                                          Write( uint32_t offset, uint32_t size, void const* data ) const;
  [[nodiscard]] ID3D12Resource*                 GetBuffer() const noexcept;
  [[nodiscard]] uint32_t                        GetSize() const noexcept;
  [[nodiscard]] uint32_t                        GetOffset() const noexcept;
  [[nodiscard]] Type                            GetType() const noexcept;

  [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW const& GetVertexBufferView() const noexcept;
  [[nodiscard]] D3D12_INDEX_BUFFER_VIEW const&  GetIndexBufferView() const noexcept;

  static Buffer CreateVertexBuffer( D3D12MA::Allocator* allocator, uint32_t size, uint32_t stride );
  static Buffer CreateIndexBuffer( D3D12MA::Allocator* allocator, uint32_t size, DXGI_FORMAT format );
};

} // namespace Ember
