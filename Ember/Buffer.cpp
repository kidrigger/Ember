#include "Buffer.hpp"

#include "RenderDevice.hpp"
#include "Util/HelperUtils.hpp"

Ember::Buffer::StorageBufferInfoImpl::StorageBufferInfoImpl(
    BindlessManager* const bindless, SRVHandle srv_handle, UAVHandle uav_handle )
  : Bindless{ bindless }, AsSRV{ std::move( srv_handle ) }, AsUAV{ std::move( uav_handle ) }
{}

Ember::Buffer::StorageBufferInfoImpl::StorageBufferInfoImpl( StorageBufferInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsSRV{ other.AsSRV }, AsUAV{ other.AsUAV }
{
  other.AsSRV = {};
  other.AsUAV = {};
}

Ember::Buffer::StorageBufferInfoImpl& Ember::Buffer::StorageBufferInfoImpl::operator=(
    StorageBufferInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsSRV, other.AsSRV );
  std::swap( AsUAV, other.AsUAV );
  return *this;
}

Ember::Buffer::StorageBufferInfoImpl::~StorageBufferInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsSRV );
  Bindless->Free( AsUAV );
}

Ember::Buffer::ConstantBufferInfoImpl::ConstantBufferInfoImpl( BindlessManager* const bindless, CBVHandle cbv_handle )
  : Bindless{ bindless }, AsCBV{ std::exchange( cbv_handle, {} ) }
{}

Ember::Buffer::ConstantBufferInfoImpl::ConstantBufferInfoImpl( ConstantBufferInfoImpl&& other ) noexcept
  : Bindless{ other.Bindless }, AsCBV{ std::exchange( other.AsCBV, {} ) }
{}

Ember::Buffer::ConstantBufferInfoImpl& Ember::Buffer::ConstantBufferInfoImpl::operator=(
    ConstantBufferInfoImpl&& other ) noexcept
{
  if ( this == &other ) return *this;
  std::swap( Bindless, other.Bindless );
  std::swap( AsCBV, other.AsCBV );
  return *this;
}

Ember::Buffer::ConstantBufferInfoImpl::~ConstantBufferInfoImpl()
{
  if ( not Bindless ) return;

  Bindless->Free( AsCBV );
}

Ember::Buffer::Buffer(
    ComPtr<ID3D12Resource>      buffer,
    ComPtr<D3D12MA::Allocation> allocation,
    uint32_t const              offset,
    uint32_t const              size,
    D3D12_GPU_VIRTUAL_ADDRESS   gpu_address,
    Views                       view )
  : m_Buffer{ std::move( buffer ) }
  , m_Allocation{ std::move( allocation ) }
  , m_Offset{ offset }
  , m_Size{ size }
  , m_GPUAddress{ gpu_address }
  , m_Views{ std::move( view ) }
{}

void Ember::Buffer::Write( uint32_t const offset, uint32_t const size, void const* data ) const
{
  if ( size == 0 ) return;

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
  return m_Size;
}

uint32_t Ember::Buffer::GetOffset() const noexcept
{
  return m_Offset;
}

Ember::Buffer::Type Ember::Buffer::GetType() const noexcept
{
  ASSERT( m_Buffer );
  return ( Type )( m_Views.index() );
}

D3D12_VERTEX_BUFFER_VIEW const& Ember::Buffer::GetVertexBufferView() const noexcept
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kVertexBuffer );

  return std::get<D3D12_VERTEX_BUFFER_VIEW>( m_Views );
}

D3D12_INDEX_BUFFER_VIEW const& Ember::Buffer::GetIndexBufferView() const noexcept
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kIndexBuffer );

  return std::get<D3D12_INDEX_BUFFER_VIEW>( m_Views );
}

Ember::SRVHandle Ember::Buffer::GetSRVHandle() const
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kStorageBuffer );

  return std::get<StorageBufferInfo>( m_Views )->AsSRV;
}

Ember::UAVHandle Ember::Buffer::GetUAVHandle() const
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kStorageBuffer );

  UAVHandle const handle = std::get<StorageBufferInfo>( m_Views )->AsUAV;
  ASSERT( handle );

  return handle;
}

Ember::CBVHandle Ember::Buffer::GetCBVHandle() const
{
  ASSERT( m_Buffer );
  ASSERT( GetType() == Type::kConstantBuffer );

  return std::get<ConstantBufferInfo>( m_Views )->AsCBV;
}

void Ember::Buffer::SetName( LPCWSTR const name ) const
{
  ERR_ABORT( m_Buffer->SetName( name ) );
}

D3D12_GPU_VIRTUAL_ADDRESS Ember::Buffer::GetGPUVirtualAddress() const
{
  return m_GPUAddress;
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

  D3D12_VERTEX_BUFFER_VIEW const vertex_buffer_view = {
    .BufferLocation = buffer->GetGPUVirtualAddress(),
    .SizeInBytes    = size,
    .StrideInBytes  = stride,
  };

  return Buffer{ std::move( buffer ), std::move( allocation ), 0, size, vertex_buffer_view.BufferLocation,
                 vertex_buffer_view };
}

Ember::Buffer Ember::BufferManager::CreateIndexBuffer( uint32_t const size, DXGI_FORMAT const format )
{
  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  D3D12_INDEX_BUFFER_VIEW const index_buffer_view = {
    .BufferLocation = buffer->GetGPUVirtualAddress(),
    .SizeInBytes    = size,
    .Format         = format,
  };

  return Buffer{
    std::move( buffer ), std::move( allocation ), 0, size, index_buffer_view.BufferLocation, index_buffer_view,
  };
}

Ember::Buffer Ember::BufferManager::CreateStorageBuffer( uint32_t const size, uint32_t const stride )
{
  ASSERT( size % stride == 0 );

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl( m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer );

  CD3DX12_SHADER_RESOURCE_VIEW_DESC const srv_desc =
      CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer( size / stride, stride );

  SRVHandle const srv_handle   = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );

  auto            storage_info = std::allocate_shared<Buffer::StorageBufferInfoImpl>(
      std::pmr::polymorphic_allocator<byte>{ &m_MemoryPool }, m_Bindless, srv_handle, UAVHandle{} );

  return Buffer{
    std::move( buffer ), std::move( allocation ), 0, size, buffer->GetGPUVirtualAddress(), std::move( storage_info ),
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

  auto srv_desc                = CD3DX12_SHADER_RESOURCE_VIEW_DESC::RawBuffer( size / kStride, 0 );
  srv_desc.Format              = DXGI_FORMAT_R32_TYPELESS;

  SRVHandle const srv_handle   = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );

  auto            storage_info = std::allocate_shared<Buffer::StorageBufferInfoImpl>(
      std::pmr::polymorphic_allocator<byte>{ &m_MemoryPool }, m_Bindless, srv_handle, UAVHandle{} );

  return Buffer{
    std::move( buffer ), std::move( allocation ), 0, size, buffer->GetGPUVirtualAddress(), std::move( storage_info ),
  };
}

Ember::Buffer Ember::BufferManager::CreateReadWriteBuffer( uint32_t size, uint32_t stride )
{
  ASSERT( size % stride == 0 );

  ComPtr<ID3D12Resource>      buffer;
  ComPtr<D3D12MA::Allocation> allocation;
  AllocateBufferImpl(
      m_Device.Get(), m_GpuAllocator.Get(), size, &allocation, &buffer, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

  CD3DX12_SHADER_RESOURCE_VIEW_DESC const srv_desc =
      CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer( size / stride, stride );

  CD3DX12_UNORDERED_ACCESS_VIEW_DESC const uav_desc =
      CD3DX12_UNORDERED_ACCESS_VIEW_DESC::StructuredBuffer( size / stride, stride );

  SRVHandle const srv_handle   = m_Bindless->CreateDescriptorHandle( buffer.Get(), srv_desc );
  UAVHandle const uav_handle   = m_Bindless->CreateDescriptorHandle( buffer.Get(), uav_desc );

  auto            storage_info = std::allocate_shared<Buffer::StorageBufferInfoImpl>(
      std::pmr::polymorphic_allocator<byte>{ &m_MemoryPool }, m_Bindless, srv_handle, uav_handle );

  return Buffer{
    std::move( buffer ), std::move( allocation ), 0, size, buffer->GetGPUVirtualAddress(), std::move( storage_info ),
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

  CBVHandle const cbv_handle           = m_Bindless->CreateDescriptorHandle( desc );

  auto            constant_buffer_info = std::allocate_shared<Buffer::ConstantBufferInfoImpl>(
      std::pmr::polymorphic_allocator<byte>{ &m_MemoryPool }, m_Bindless, cbv_handle );

  return Buffer{
    std::move( buffer ), std::move( allocation ),        0,
    padded_size,         buffer->GetGPUVirtualAddress(), std::move( constant_buffer_info ),
  };
}
