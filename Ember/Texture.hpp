#pragma once

#include <optional>


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
  struct SampledInfoImpl
  {
    BindlessManager* Bindless;
    SRVHandle        AsSRV;

    SampledInfoImpl( BindlessManager* const bindless, SRVHandle as_srv );

    SampledInfoImpl( SampledInfoImpl const& other ) = delete;
    SampledInfoImpl( SampledInfoImpl&& other ) noexcept;
    SampledInfoImpl& operator=( SampledInfoImpl const& other ) = delete;
    SampledInfoImpl& operator=( SampledInfoImpl&& other ) noexcept;
    ~SampledInfoImpl();
  };
  using SampledInfo = std::shared_ptr<SampledInfoImpl>;

  struct StorageInfoImpl
  {
    BindlessManager* Bindless;
    SRVHandle        AsSRV;
    UAVHandle        AsUAV;

    StorageInfoImpl( BindlessManager* bindless, SRVHandle as_srv, UAVHandle as_uav );
    StorageInfoImpl( StorageInfoImpl const& other ) = delete;
    StorageInfoImpl( StorageInfoImpl&& other ) noexcept;
    StorageInfoImpl& operator=( StorageInfoImpl const& other ) = delete;
    StorageInfoImpl& operator=( StorageInfoImpl&& other ) noexcept;
    ~StorageInfoImpl();
  };
  using StorageInfo = std::shared_ptr<StorageInfoImpl>;

  struct DepthInfoImpl
  {
    BindlessManager*              Bindless;
    SRVHandle                     AsSRV;
    D3D12_DEPTH_STENCIL_VIEW_DESC AsDSVDesc;

    DepthInfoImpl( BindlessManager* bindless, SRVHandle as_srv, D3D12_DEPTH_STENCIL_VIEW_DESC view );

    DepthInfoImpl( DepthInfoImpl const& other ) = delete;
    DepthInfoImpl( DepthInfoImpl&& other ) noexcept;
    DepthInfoImpl& operator=( DepthInfoImpl const& other ) = delete;
    DepthInfoImpl& operator=( DepthInfoImpl&& other ) noexcept;

    ~DepthInfoImpl();
  };
  using DepthInfo = std::shared_ptr<DepthInfoImpl>;

  struct AttachmentInfoImpl
  {
    BindlessManager*              Bindless;
    SRVHandle                     AsSRV;
    D3D12_RENDER_TARGET_VIEW_DESC AsRTVDesc;

    AttachmentInfoImpl( BindlessManager* const bindless, SRVHandle as_srv, D3D12_RENDER_TARGET_VIEW_DESC as_rtv_desc );
    AttachmentInfoImpl( AttachmentInfoImpl const& other ) = delete;
    AttachmentInfoImpl( AttachmentInfoImpl&& other ) noexcept;
    AttachmentInfoImpl& operator=( AttachmentInfoImpl const& other ) = delete;
    AttachmentInfoImpl& operator=( AttachmentInfoImpl&& other ) noexcept;
    ~AttachmentInfoImpl();
  };
  using AttachmentInfo = std::shared_ptr<AttachmentInfoImpl>;

  enum class Type
  {
    kSampled,
    kStorage,
    kDepth,
    kAttachment,
  };

private:
  using Views = std::variant<SampledInfo, StorageInfo, DepthInfo, AttachmentInfo>;

  ComPtr<ID3D12Resource>      m_Texture;
  ComPtr<D3D12MA::Allocation> m_Allocation;
  Views                       m_Views;

public:
  Texture() = default;

  Texture( ComPtr<ID3D12Resource> texture, ComPtr<D3D12MA::Allocation> allocation, Views views );

  operator bool() const;
  [[nodiscard]] ID3D12Resource*                      GetTexture() const;
  [[nodiscard]] D3D12MA::Allocation*                 GetAllocation() const;
  [[nodiscard]] Type                                 GetType() const noexcept;
  void                                               SetName( LPCWSTR name ) const;

  [[nodiscard]] SRVHandle                            GetSRVHandle() const;
  [[nodiscard]] UAVHandle                            GetUAVHandle() const;
  [[nodiscard]] D3D12_DEPTH_STENCIL_VIEW_DESC const* GetDepthStencilView() const;
  [[nodiscard]] D3D12_RENDER_TARGET_VIEW_DESC const* GetRenderTargetView() const;
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

struct Texture2DCreateInfo
{
  DXGI_FORMAT                          Format;
  uint32_t                             Width;
  uint32_t                             Height;
  TextureUsage                         Usage{ TextureUsage::kReadonly };
  MipLevels                            MipLevels{ MipLevels::kAuto };
  std::optional<D3D12_RESOURCE_STATES> InitState;
};

struct TextureCubeCreateInfo
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

public:
  TextureManager() = default;
  TextureManager(
      ComPtr<ID3D12Device2> device, ComPtr<D3D12MA::Allocator> allocator, BindlessManager* bindless_manager );

  Texture CreateDepthTexture2D( Texture2DCreateInfo const& create_info );
  Texture CreateRenderTexture2D( Texture2DCreateInfo const& create_info );
  Texture CreateTexture2D( Texture2DCreateInfo const& create_info );

  Texture CreateDepthTextureCube( TextureCubeCreateInfo const& create_info );
  Texture CreateTextureCube( TextureCubeCreateInfo const& create_info );

  Sampler CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );
};

} // namespace Ember
