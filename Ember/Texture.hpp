#pragma once

#include "BindlessHandle.hpp"
#include "BindlessManager.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class Sampler
{
public:
  struct SamplerInfoImpl
  {
    BindlessManager* Bindless;
    SamplerHandle    Handle;

    SamplerInfoImpl( BindlessManager* bindless, SamplerHandle handle );
    SamplerInfoImpl( SamplerInfoImpl const& other ) = delete;
    SamplerInfoImpl( SamplerInfoImpl&& other ) noexcept;
    SamplerInfoImpl& operator=( SamplerInfoImpl const& other ) = delete;
    SamplerInfoImpl& operator=( SamplerInfoImpl&& other ) noexcept;
    ~SamplerInfoImpl();
  };
  using SamplerInfo = std::shared_ptr<SamplerInfoImpl>;

private:
  SamplerInfo m_SamplerInfo;

public:
  Sampler() = default;
  explicit Sampler( SamplerInfo sampler_handle );

  [[nodiscard]] SamplerHandle GetSamplerHandle() const;
};

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
  ComPtr<D3D12MA::Allocator>           m_Allocator;

public:
  TextureManager() = default;
  TextureManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* bindless_manager );

  Texture CreateTexture2D( DXGI_FORMAT format, uint32_t width, uint32_t height );
  Sampler CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );
};

} // namespace Ember
