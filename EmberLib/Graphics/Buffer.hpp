#pragma once

#include <memory_resource>
#include <variant>

#include "DeviceHandle.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class BindlessManager;

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

  struct StorageBufferInfoImpl
  {
    BindlessManager* Bindless;
    SRVHandle        AsSRV;
    UAVHandle        AsUAV;

    StorageBufferInfoImpl( BindlessManager* const bindless, SRVHandle srv_handle, UAVHandle uav_handle );
    StorageBufferInfoImpl( StorageBufferInfoImpl const& other ) = delete;
    StorageBufferInfoImpl( StorageBufferInfoImpl&& other ) noexcept;
    StorageBufferInfoImpl& operator=( StorageBufferInfoImpl const& other ) = delete;
    StorageBufferInfoImpl& operator=( StorageBufferInfoImpl&& other ) noexcept;
    ~StorageBufferInfoImpl();
  };
  using StorageBufferInfo = std::shared_ptr<StorageBufferInfoImpl>;

  struct ConstantBufferInfoImpl
  {
    BindlessManager* Bindless;
    CBVHandle        AsCBV;

    ConstantBufferInfoImpl( BindlessManager* bindless, CBVHandle cbv_handle );
    ConstantBufferInfoImpl( ConstantBufferInfoImpl const& other ) = delete;
    ConstantBufferInfoImpl( ConstantBufferInfoImpl&& other ) noexcept;
    ConstantBufferInfoImpl& operator=( ConstantBufferInfoImpl const& other ) = delete;
    ConstantBufferInfoImpl& operator=( ConstantBufferInfoImpl&& other ) noexcept;
    ~ConstantBufferInfoImpl();
  };
  using ConstantBufferInfo = std::shared_ptr<ConstantBufferInfoImpl>;

private:
  using Views = std::variant<D3D12_VERTEX_BUFFER_VIEW, D3D12_INDEX_BUFFER_VIEW, StorageBufferInfo, ConstantBufferInfo>;

  ComPtr<ID3D12Resource>      m_Buffer;
  ComPtr<D3D12MA::Allocation> m_Allocation;
  uint32_t                    m_Offset{ 0 };
  uint32_t                    m_Size{ 0 };
  D3D12_GPU_VIRTUAL_ADDRESS   m_GPUAddress{ 0 };
  Views                       m_Views;

public:
  Buffer() = default;

  Buffer(
      ComPtr<ID3D12Resource>      buffer,
      ComPtr<D3D12MA::Allocation> allocation,
      uint32_t                    offset,
      uint32_t                    size,
      D3D12_GPU_VIRTUAL_ADDRESS   gpu_address,
      Views                       view );

  void                                          Write( uint32_t offset, uint32_t size, void const* data ) const;
  [[nodiscard]] ID3D12Resource*                 GetBuffer() const noexcept;
  [[nodiscard]] uint32_t                        GetSize() const noexcept;
  [[nodiscard]] uint32_t                        GetOffset() const noexcept;
  [[nodiscard]] Type                            GetType() const noexcept;

  [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW const& GetVertexBufferView() const noexcept;
  [[nodiscard]] D3D12_INDEX_BUFFER_VIEW const&  GetIndexBufferView() const noexcept;
  [[nodiscard]] SRVHandle                       GetSRVHandle() const;
  [[nodiscard]] UAVHandle                       GetUAVHandle() const;
  [[nodiscard]] CBVHandle                       GetCBVHandle() const;
  void                                          SetName( LPCWSTR name ) const;

  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS       GetGPUVirtualAddress() const;
};

class BufferManager
{
  std::pmr::synchronized_pool_resource m_MemoryPool;
  BindlessManager*                     m_Bindless{ nullptr };
  ComPtr<ID3D12Device2>                m_Device;
  ComPtr<D3D12MA::Allocator>           m_GpuAllocator;

public:
  BufferManager() = default;
  explicit BufferManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> gpu_allocator, BindlessManager* bindless_manager );

  Buffer CreateVertexBuffer( uint32_t size, uint32_t stride );
  Buffer CreateIndexBuffer( uint32_t size, DXGI_FORMAT format );
  Buffer CreateStorageBuffer( uint32_t size, uint32_t stride );
  Buffer CreateRawStorageBuffer( uint32_t size );
  Buffer CreateReadWriteBuffer( uint32_t size, uint32_t stride );
  Buffer CreateConstantBuffer( uint32_t size );
};

} // namespace Ember
