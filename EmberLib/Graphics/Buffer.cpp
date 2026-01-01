#include "Buffer.hpp"

#include "RenderDevice.hpp"
#include "Util/FlatMap.hpp"
#include "Util/HelperUtils.hpp"

namespace Ember
{

struct BufferImpl
{
  struct Handles
  {
    BindlessManager* Bindless; // Not owned; To be used with the handles
    SRVHandle        AsSRV;
    UAVHandle        AsUAV;
    CBVHandle        AsCBV;

    Handles() = default;
    Handles( BindlessManager* const bindless, SRVHandle const srv, UAVHandle const uav = {} )
      : Bindless{ bindless }, AsSRV{ srv }, AsUAV{ uav }, AsCBV{}
    {}

    Handles( BindlessManager* const bindless, CBVHandle const cbv )
      : Bindless{ bindless }, AsSRV{}, AsUAV{}, AsCBV{ cbv }
    {}

    Handles( Handles&& other ) noexcept
      : Bindless{ other.Bindless }, AsSRV{ other.AsSRV }, AsUAV{ other.AsUAV }, AsCBV{ other.AsCBV }
    {
      other.Bindless = nullptr;
      other.AsSRV    = {};
      other.AsUAV    = {};
      other.AsCBV    = {};
    }

    Handles& operator=( Handles&& other ) noexcept
    {
      if ( this == &other ) return *this;
      std::swap( Bindless, other.Bindless );
      std::swap( AsSRV, other.AsSRV );
      std::swap( AsUAV, other.AsUAV );
      std::swap( AsCBV, other.AsCBV );
      return *this;
    }

    Handles( Handles const& )            = delete;
    Handles& operator=( Handles const& ) = delete;

    ~Handles()
    {
      if ( not Bindless ) return;
      if ( AsSRV ) Bindless->Free( AsSRV );
      if ( AsUAV ) Bindless->Free( AsUAV );
      if ( AsCBV ) Bindless->Free( AsCBV );
    }
  };

  ComPtr<ID3D12Resource>      Resource;
  ComPtr<D3D12MA::Allocation> Allocation;
  D3D12_GPU_VIRTUAL_ADDRESS   GPUAddress;
  uint32_t                    Offset;
  uint32_t                    Size;
  Buffer::Type                Type;
  Handles                     Handles;
};
} // namespace Ember

Ember::Buffer::Buffer( std::shared_ptr<BufferImpl> impl ) : m_Impl{ std::move( impl ) }
{}

void Ember::Buffer::Write( uint32_t const offset, uint32_t const size, void const* data ) const
{
  if ( size == 0 ) return;

  uint32_t const    absolute_offset  = GetOffset() + offset;
  D3D12_RANGE const empty_read_range = { 0, 0 };
  D3D12_RANGE const write_range      = { absolute_offset, absolute_offset + size };

  byte*             mapped;
  ERR_ABORT( m_Impl->Resource->Map( 0, &empty_read_range, ( void** )&mapped ) );

  memcpy( mapped + absolute_offset, data, size );

  m_Impl->Resource->Unmap( 0, &write_range );
}

ID3D12Resource* Ember::Buffer::GetBuffer() const noexcept
{
  ASSERT( m_Impl );
  return m_Impl->Resource.Get();
}

uint32_t Ember::Buffer::GetSize() const noexcept
{
  return m_Impl ? m_Impl->Size : 0;
}

uint32_t Ember::Buffer::GetOffset() const noexcept
{
  return m_Impl ? m_Impl->Offset : 0;
}

Ember::Buffer::Type Ember::Buffer::GetType() const noexcept
{
  ASSERT( m_Impl );
  return m_Impl->Type;
}

Ember::SRVHandle Ember::Buffer::GetSRVHandle() const
{
  ASSERT( m_Impl );
  ASSERT( GetType() == Type::kStorageBuffer );

  return m_Impl->Handles.AsSRV;
}

Ember::UAVHandle Ember::Buffer::GetUAVHandle() const
{
  ASSERT( m_Impl );
  ASSERT( GetType() == Type::kStorageBuffer );

  auto handle = m_Impl->Handles.AsUAV;
  ASSERT( handle );

  return handle;
}

Ember::CBVHandle Ember::Buffer::GetCBVHandle() const
{
  ASSERT( m_Impl );
  ASSERT( GetType() == Type::kConstantBuffer );

  return m_Impl->Handles.AsCBV;
}

void Ember::Buffer::SetName( LPCWSTR const name ) const
{
  ERR_ABORT( m_Impl->Resource->SetName( name ) );
}

D3D12_GPU_VIRTUAL_ADDRESS Ember::Buffer::GetGPUVirtualAddress() const
{
  return m_Impl->GPUAddress;
}

Ember::BufferManager::BufferManager(
    ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> gpu_allocator, BindlessManager* bindless_manager )
  : m_Bindless{ bindless_manager }, m_Device{ std::move( device ) }, m_GpuAllocator{ std::move( gpu_allocator ) }
{}

namespace
{

void AllocateBufferImpl(
    [[maybe_unused]] ID3D12Device2* device, // Only used when running for RenderDoc
    D3D12MA::Allocator*             allocator,
    uint32_t const                  size,
    D3D12MA::Allocation**           allocation,
    ID3D12Resource**                resource,
    D3D12_RESOURCE_FLAGS const      flags = D3D12_RESOURCE_FLAG_NONE )
{
  CD3DX12_RESOURCE_DESC const buffer_desc = CD3DX12_RESOURCE_DESC::Buffer( size, flags );

  // TODO: Buffer Allocation can fail. Handle by returning value.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::ALLOCATION_DESC constexpr allocation_desc = {
    .Flags    = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
  };

  ERR_ABORT( allocator->CreateResource(
      &allocation_desc, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, allocation, IID_PPV_ARGS( resource ) ) );
#else
  auto heap_properties = CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_GPU_UPLOAD };
  ERR_ABORT( device->CreateCommittedResource(
      &heap_properties,
      D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
      &buffer_desc,
      D3D12_RESOURCE_STATE_COMMON,
      nullptr,
      IID_PPV_ARGS( resource ) ) );
#endif
}
} // namespace

Ember::Buffer Ember::BufferManager::CreateVertexBuffer( uint32_t const size, uint32_t const stride )
{
  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  auto const virtual_address = buffer->GetGPUVirtualAddress();
  return Buffer{ std::allocate_shared<BufferImpl>(
      GetAllocator(),
      BufferImpl{
          .Resource   = std::move( buffer ),
          .Allocation = std::move( allocation ),
          .GPUAddress = virtual_address,
          .Offset     = 0,
          .Size       = size,
          .Type       = Buffer::Type::kVertexBuffer,
      } ) };
}

Ember::Buffer Ember::BufferManager::CreateIndexBuffer( uint32_t const size, DXGI_FORMAT const format )
{
  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  auto const virtual_address = buffer->GetGPUVirtualAddress();
  return Buffer{ std::allocate_shared<BufferImpl>(
      GetAllocator(),
      BufferImpl{
          .Resource   = std::move( buffer ),
          .Allocation = std::move( allocation ),
          .GPUAddress = virtual_address,
          .Offset     = 0,
          .Size       = size,
          .Type       = Buffer::Type::kIndexBuffer,
      } ) };
}

Ember::Buffer Ember::BufferManager::CreateStorageBuffer( uint32_t const size, uint32_t const stride )
{
  ASSERT( size % stride == 0 );

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  auto const srv_desc        = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer( size / stride, stride );

  auto const srv_handle      = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );
  auto const virtual_address = buffer->GetGPUVirtualAddress();

  return Buffer{
    std::allocate_shared<BufferImpl>(
        GetAllocator(),
        BufferImpl{
                   .Resource   = std::move( buffer ),
                   .Allocation = std::move( allocation ),
                   .GPUAddress = virtual_address,
                   .Offset     = 0,
                   .Size       = size,
                   .Type       = Buffer::Type::kStorageBuffer,
                   .Handles    = { m_Bindless, srv_handle },
                   }
        )
  };
}

Ember::Buffer Ember::BufferManager::CreateRawStorageBuffer( uint32_t size )
{
  uint32_t constexpr static kStride = 4;

  //
  size = ( size % kStride == 0 ) ? size : size + ( kStride - ( size % kStride ) );

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  auto srv_desc              = CD3DX12_SHADER_RESOURCE_VIEW_DESC::RawBuffer( size / kStride, 0 );
  srv_desc.Format            = DXGI_FORMAT_R32_TYPELESS;

  auto const srv_handle      = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );
  auto const virtual_address = buffer->GetGPUVirtualAddress();

  return Buffer{
    std::allocate_shared<BufferImpl>(
        GetAllocator(),
        BufferImpl{
                   .Resource   = std::move( buffer ),
                   .Allocation = std::move( allocation ),
                   .GPUAddress = virtual_address,
                   .Offset     = 0,
                   .Size       = size,
                   .Type       = Buffer::Type::kStorageBuffer,
                   .Handles    = { m_Bindless, srv_handle },
                   }
        )
  };
}

Ember::Buffer Ember::BufferManager::CreateReadWriteBuffer( uint32_t size, uint32_t stride )
{
  ASSERT( size % stride == 0 );

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl(
      m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

  auto const srv_desc        = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer( size / stride, stride );
  auto const uav_desc        = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::StructuredBuffer( size / stride, stride );

  auto const srv_handle      = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );
  auto const uav_handle      = m_Bindless->CreateDescriptorHandle( buffer.Get(), uav_desc );
  auto const virtual_address = buffer->GetGPUVirtualAddress();

  return Buffer{
    std::allocate_shared<BufferImpl>(
        GetAllocator(),
        BufferImpl{
                   .Resource   = std::move( buffer ),
                   .Allocation = std::move( allocation ),
                   .GPUAddress = virtual_address,
                   .Offset     = 0,
                   .Size       = size,
                   .Type       = Buffer::Type::kStorageBuffer,
                   .Handles    = { m_Bindless, srv_handle, uav_handle },
                   }
        )
  };
}

Ember::Buffer Ember::BufferManager::CreateConstantBuffer( uint32_t const size )
{
  uint32_t const              padded_size = ( 256 - ( size % 256 ) ) + size;

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), padded_size, &allocation, &buffer );

  D3D12_CONSTANT_BUFFER_VIEW_DESC const desc = {
    .BufferLocation = buffer->GetGPUVirtualAddress(),
    .SizeInBytes    = padded_size,
  };

  auto const cbv_handle = m_Bindless->CreateDescriptorHandle( desc );

  return Buffer{
    std::allocate_shared<BufferImpl>(
        GetAllocator(),
        BufferImpl{
                   .Resource   = std::move( buffer ),
                   .Allocation = std::move( allocation ),
                   .GPUAddress = desc.BufferLocation,
                   .Offset     = 0,
                   .Size       = size,
                   .Type       = Buffer::Type::kConstantBuffer,
                   .Handles    = { m_Bindless, cbv_handle },
                   }
        )
  };
}

std::pmr::polymorphic_allocator<> Ember::BufferManager::GetAllocator()
{
  return std::pmr::polymorphic_allocator<>( &m_MemoryPool );
}
