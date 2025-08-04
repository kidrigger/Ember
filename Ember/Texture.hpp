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

  operator bool() const;

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

  operator bool() const;
  [[nodiscard]] ID3D12Resource*      GetTexture() const;
  [[nodiscard]] D3D12MA::Allocation* GetAllocation() const;
  [[nodiscard]] SRVHandle            GetSRVHandle() const;
  [[nodiscard]] UAVHandle            GetUAVHandle() const;
  void                               SetName( LPCWSTR name ) const;
};

enum class TextureUsage
{
  kReadonly,
  kReadWrite,
};

class MipLevels
{
  uint16_t m_Value;

public:
  MipLevels( uint16_t levels = 0 );

  constexpr static uint16_t kAuto = 0;
  constexpr static uint16_t kBase = 1;
  operator UINT16() const;
};

struct Texture2DCreateInfo
{
  DXGI_FORMAT  Format;
  uint32_t     Width;
  uint32_t     Height;
  TextureUsage Usage{ TextureUsage::kReadonly };
  MipLevels    MipLevels{ MipLevels::kAuto };
};

struct TextureCubeCreateInfo
{
  DXGI_FORMAT  Format;
  uint32_t     Side;
  TextureUsage Usage{ TextureUsage::kReadonly };
  MipLevels    MipLevels{ MipLevels::kAuto };
};

class TextureManager
{
  std::pmr::synchronized_pool_resource m_MemoryPool;
  BindlessManager*                     m_Bindless{ nullptr };
  ComPtr<ID3D12Device2>                m_Device;
  ComPtr<D3D12MA::Allocator>           m_Allocator;

  //
  void CreateResourceImpl(
      ID3D12Resource** texture, D3D12MA::Allocation** allocation, CD3DX12_RESOURCE_DESC const& resource_desc );

public:
  TextureManager() = default;
  TextureManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* bindless_manager );

  Texture CreateTexture2D( Texture2DCreateInfo const& create_info );
  Texture CreateTextureCube( TextureCubeCreateInfo const& create_info );
  Sampler CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );
};

} // namespace Ember
