#pragma once

#include <memory>
#include <memory_resource>
#include <optional>
#include <variant>

#include "BindlessManager.hpp"
#include "DeviceHandle.hpp"
#include "Util/DirectXHeaders.hpp"

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

struct TextureImpl;

class Texture
{
public:
  enum class Type
  {
    kSampled,
    kStorage,
    kDepth,
    kAttachment,
  };

private:
  std::shared_ptr<TextureImpl> m_Impl;

public:
  Texture() = default;

  Texture( std::shared_ptr<TextureImpl> impl );

  operator bool() const;
  [[nodiscard]] ID3D12Resource*       GetTexture() const;
  [[nodiscard]] D3D12MA::Allocation*  GetAllocation() const;
  [[nodiscard]] Type                  GetType() const noexcept;
  [[nodiscard]] D3D12_RESOURCE_STATES GetCurrentState() const noexcept;
  void                                SetCurrentState( D3D12_RESOURCE_STATES state ) const noexcept;
  void                                SetName( LPCWSTR name ) const;

  [[nodiscard]] SRVHandle             GetSRVHandle() const;
  [[nodiscard]] UAVHandle             GetUAVHandle() const;

  [[nodiscard]] uintptr_t             GetPtrID() const;
};

enum class TextureUsage
{
  kReadonly,
  kReadWrite,
  kDepthSample,
  kRenderTarget,
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

struct Tex2DDesc
{
  DXGI_FORMAT                          Format;
  uint32_t                             Width;
  uint32_t                             Height;
  TextureUsage                         Usage{ TextureUsage::kReadonly };
  MipLevels                            MipLevels{ MipLevels::kAuto };
  uint16_t                             ArraySize{ 1 };
  std::optional<D3D12_RESOURCE_STATES> InitState;
};

struct TexCubeDesc
{
  DXGI_FORMAT                          Format;
  uint32_t                             Side;
  TextureUsage                         Usage{ TextureUsage::kReadonly };
  MipLevels                            MipLevels{ MipLevels::kAuto };
  std::optional<D3D12_RESOURCE_STATES> InitState;
};

class TextureManager
{
  std::pmr::synchronized_pool_resource m_MemoryPool;
  BindlessManager*                     m_Bindless{ nullptr };
  ComPtr<ID3D12Device2>                m_Device;
  ComPtr<D3D12MA::Allocator>           m_Allocator;

  //
  void CreateResourceImpl(
      ID3D12Resource**             texture,
      D3D12MA::Allocation**        allocation,
      CD3DX12_RESOURCE_DESC const& resource_desc,
      D3D12_CLEAR_VALUE const*     clear_value = nullptr,
      D3D12_RESOURCE_STATES initial_states     = D3D12_RESOURCE_STATE_COMMON ) const;

  Texture CreateDepthTexture2D( Tex2DDesc const& create_info );
  Texture CreateRenderTexture2D( Tex2DDesc const& create_info );
  Texture CreateDepthTextureCube( TexCubeDesc const& create_info );

public:
  TextureManager() = default;
  TextureManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* bindless_manager );

  Texture                           CreateTexture2D( Tex2DDesc const& create_info );
  Texture                           CreateTextureCube( TexCubeDesc const& create_info );

  Sampler                           CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );

  std::pmr::polymorphic_allocator<> GetAllocator() noexcept;
};

} // namespace Ember
