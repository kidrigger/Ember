#pragma once

#include <memory_resource>
#include <variant>

#include "DeviceHandle.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class BindlessManager;

struct BufferImpl;

class Buffer
{
public:
  enum class Type : uint8_t
  {
    kVertexBuffer          = 0,
    kIndexBuffer           = 1,
    kStorageBuffer         = 2,
    kConstantBuffer        = 3,
    kAccelerationStructure = 4,
  };

private:
  std::shared_ptr<BufferImpl> m_Impl;

public:
  Buffer() = default;

  Buffer( std::shared_ptr<BufferImpl> impl );

  void                                    Write( uint32_t offset, uint32_t size, void const* data ) const;
  [[nodiscard]] ID3D12Resource*           GetBuffer() const noexcept;
  [[nodiscard]] uint64_t                  GetSize() const noexcept;
  [[nodiscard]] Type                      GetType() const noexcept;

  [[nodiscard]] SRVHandle                 GetSRVHandle() const;
  [[nodiscard]] UAVHandle                 GetUAVHandle() const;
  [[nodiscard]] CBVHandle                 GetCBVHandle() const;
  void                                    SetName( LPCWSTR name ) const;

  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;
  [[nodiscard]] uintptr_t                 GetPtrID() const;
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

  Buffer                            CreateVertexBuffer( uint32_t size, uint32_t stride );
  Buffer                            CreateIndexBuffer( uint32_t size, DXGI_FORMAT format );
  Buffer                            CreateStorageBuffer( uint32_t size, uint32_t stride );
  Buffer                            CreateRawStorageBuffer( uint32_t size );
  Buffer                            CreateReadWriteBuffer( uint32_t size, uint32_t stride );
  Buffer                            CreateConstantBuffer( uint32_t size );
  Buffer                            CreateASBuffer( uint64_t size );

  std::pmr::polymorphic_allocator<> GetAllocator();
};

} // namespace Ember
