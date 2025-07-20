#include "Buffer.hpp"

#include "RenderDevice.hpp"
#include "Util/HelperUtils.hpp"

Ember::Buffer::Buffer(
    ComPtr<ID3D12Resource>      buffer,
    ComPtr<D3D12MA::Allocation> allocation,
    Type const                  type,
    uint32_t const              offset,
    uint32_t const              stride,
    uint32_t const              size )
  : m_Buffer{ std::move( buffer ) }
  , m_Allocation{ std::move( allocation ) }
  , m_VirtualAddress{ m_Buffer->GetGPUVirtualAddress() }
  , m_OffsetAndType{ offset | ( uint32_t )type }
  , m_Size{ size }
{
  ASSERT_M( ( offset & kBufferTypeMask ) == 0, "Offset should be multiple of 8." );

  switch ( type )
  {
    case Type::kVertexBuffer:
    {
      m_VertexBufferView = {
        .BufferLocation = m_VirtualAddress + offset,
        .SizeInBytes    = size,
        .StrideInBytes  = stride,
      };
    }
    break;
    case Type::kIndexBuffer:
    {
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      if ( stride == 1 )
      {
        format = DXGI_FORMAT_R8_UINT;
      }
      else if ( stride == 2 )
      {
        format = DXGI_FORMAT_R16_UINT;
      }
      else if ( stride == 4 )
      {
        format = DXGI_FORMAT_R32_UINT;
      }
      ASSERT_M( format, "Invalid stride for an index buffer" );
      m_IndexBufferView = {
        .BufferLocation = m_VirtualAddress + offset,
        .SizeInBytes    = size,
        .Format         = format,
      };
    }
    break;
    case Type::kStorageBuffer:
    case Type::kConstantBuffer:
      ASSERT_M( false, "Unimplemented" );
      break;
  }
}

Ember::Buffer::Buffer(
    ComPtr<ID3D12Resource>      buffer,
    ComPtr<D3D12MA::Allocation> allocation,
    Type                        type,
    uint32_t const              offset,
    DXGI_FORMAT const           format,
    uint32_t const              size )
  : m_Buffer{ std::move( buffer ) }
  , m_Allocation{ std::move( allocation ) }
  , m_VirtualAddress{ m_Buffer->GetGPUVirtualAddress() }
  , m_OffsetAndType{ offset | ( uint32_t )type }
  , m_Size{ size }
{
  ASSERT( type == Type::kIndexBuffer );

  m_IndexBufferView = {
    .BufferLocation = m_VirtualAddress + offset,
    .SizeInBytes    = size,
    .Format         = format,
  };
}

void Ember::Buffer::Write( uint32_t const offset, uint32_t const size, void const* data ) const
{
  uint32_t const    absolute_offset  = GetOffset() + offset;
  D3D12_RANGE const empty_read_range = { 0, 0 };
  D3D12_RANGE const write_range      = { absolute_offset, absolute_offset + size };

  byte*             mapped;
  ERR_ABORT( m_Buffer->Map( 0, &empty_read_range, ( void** )&mapped ) );

  memcpy( mapped + absolute_offset, data, size );

  m_Buffer->Unmap( 0, &write_range );
}

ID3D12Resource* Ember::Buffer::GetBuffer() const noexcept
{
  ASSERT( m_Buffer );
  return m_Buffer.Get();
}

uint32_t Ember::Buffer::GetSize() const noexcept
{
  ASSERT( m_Buffer );
  return m_Size;
}

uint32_t Ember::Buffer::GetOffset() const noexcept
{
  ASSERT( m_Buffer );
  return m_OffsetAndType & kOffsetMask;
}

Ember::Buffer::Type Ember::Buffer::GetType() const noexcept
{
  ASSERT( m_Buffer );
  return ( Type )( m_OffsetAndType & kBufferTypeMask );
}

D3D12_VERTEX_BUFFER_VIEW const& Ember::Buffer::GetVertexBufferView() const noexcept
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kVertexBuffer );

  return m_VertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW const& Ember::Buffer::GetIndexBufferView() const noexcept
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kIndexBuffer );

  return m_IndexBufferView;
}

void Ember::AllocateBufferImpl(
    D3D12MA::Allocator* allocator, uint32_t const size, D3D12MA::Allocation** allocation, ID3D12Resource** resource )
{
  D3D12MA::ALLOCATION_DESC constexpr allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
  };

  CD3DX12_RESOURCE_DESC1 const buffer_desc = CD3DX12_RESOURCE_DESC1::Buffer( size );

  ERR_ABORT( allocator->CreateResource2(
      &allocation_desc, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, allocation, IID_PPV_ARGS( resource ) ) );
}

Ember::Buffer Ember::Buffer::CreateVertexBuffer(
    D3D12MA::Allocator* allocator, uint32_t const size, uint32_t const stride )
{
  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( allocator, size, &allocation, &buffer );

  return Buffer{ std::move( buffer ), std::move( allocation ), Buffer::Type::kVertexBuffer, 0, stride, size };
}

Ember::Buffer Ember::Buffer::CreateIndexBuffer(
    D3D12MA::Allocator* allocator, uint32_t const size, DXGI_FORMAT const format )
{
  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( allocator, size, &allocation, &buffer );

  return Buffer{ std::move( buffer ), std::move( allocation ), Buffer::Type::kIndexBuffer, 0, format, size };
}
