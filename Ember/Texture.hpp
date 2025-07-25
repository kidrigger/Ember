#pragma once

#include "BindlessHandle.hpp"
#include "BindlessManager.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class Texture
{
public:
  struct TextureInfoImpl
  {
    BindlessManager* Bindless;
    SRVHandle        AsSRV;
    UAVHandle        AsUAV;

    TextureInfoImpl( BindlessManager* bindless, SRVHandle as_srv, UAVHandle as_uav );
    TextureInfoImpl( TextureInfoImpl const& other ) = delete;
    TextureInfoImpl( TextureInfoImpl&& other ) noexcept;
    TextureInfoImpl& operator=( TextureInfoImpl const& other ) = delete;
    TextureInfoImpl& operator=( TextureInfoImpl&& other ) noexcept;
    ~TextureInfoImpl();
  };
  using TextureInfo = std::shared_ptr<TextureInfoImpl>;

private:
  ComPtr<ID3D12Resource>      m_Texture;
  ComPtr<D3D12MA::Allocation> m_Allocation;
  TextureInfo                 m_TextureInfo;

public:
  Texture() = default;

  Texture( ComPtr<ID3D12Resource> texture, ComPtr<D3D12MA::Allocation> allocation, TextureInfo texture_info );

  [[nodiscard]] ID3D12Resource* GetTexture() const;
  [[nodiscard]] SRVHandle       GetSRVHandle() const;
  [[nodiscard]] UAVHandle       GetUAVHandle() const;
};

class TextureManager
{
  std::pmr::synchronized_pool_resource m_MemoryPool;
  BindlessManager*                     m_Bindless{ nullptr };
  ComPtr<ID3D12Device2>                m_Device;
  ComPtr<D3D12MA::Allocator>           m_GpuAllocator;

public:
  TextureManager() = default;
  TextureManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> gpu_allocator, BindlessManager* bindless_manager );

  Texture CreateTexture2D( DXGI_FORMAT format, uint32_t width, uint32_t height );
};

} // namespace Ember
