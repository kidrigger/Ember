#pragma once

#include <queue>
#include <variant>

#include <Graphics/DeviceHandle.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/FlatMap.hpp>

#include "RenderPassCommon.hpp"

namespace Ember
{
class RenderDevice;

namespace FG
{

struct BackbufferInfo
{
  DXGI_FORMAT SwapchainFormat;
  DXGI_FORMAT DepthStencilFormat;
  uint32_t    Width;
  uint32_t    Height;
};

enum class ReadType
{
  kDSV,
  kSRV,
  kCBV,
  kCopy,
};

enum class WriteType
{
  kRTV,
  kDSV,
  kCopy,
};

struct DepthStencilRead
{
  ReadType Type = ReadType::kDSV; // 2 bits

  operator uint32_t() const;
  static DepthStencilRead Decode( uint32_t flag );
};

struct ShaderResource
{
  ReadType Type           = ReadType::kSRV; // 2 bits
  bool     PixelShaderUse = true;           // 1 bit
  bool     OnlyTopMip     = true;           // 1 bit

  operator uint32_t() const;
  static ShaderResource Decode( uint32_t flag );
};

struct CopySrc
{
  ReadType Type = ReadType::kCopy; // 2 bits

  operator uint32_t() const;
  static CopySrc Decode( uint32_t flag );
};

enum class LoadOperation : uint32_t
{
  kLoad    = 0,
  kDiscard = 1,
  kClear   = 2,
};

struct Attachment
{
  WriteType     Type      = WriteType::kRTV;      // 2 bits
  uint8_t       Index     = 0xFF;                 // 3 bits
  bool          ForceSrgb = false;                // 1 bit
  LoadOperation LoadOp    = LoadOperation::kLoad; // 2 bits

  operator uint32_t() const;
  static Attachment Decode( uint32_t flag );
};

struct DepthStencil
{
  WriteType     Type   = WriteType::kDSV;      // 2 bits
  LoadOperation LoadOp = LoadOperation::kLoad; // 2 bits

  operator uint32_t() const;
  static DepthStencil Decode( uint32_t flag );
};

struct CopyDst
{
  WriteType Type = WriteType::kCopy; // 2 bits

  operator uint32_t() const;
  static CopyDst Decode( uint32_t flag );
};

using Read = std::variant<DepthStencilRead, ShaderResource, std::monostate, CopySrc>;
Read DecodeReadFlags( uint32_t v );

using Write = std::variant<Attachment, DepthStencil, CopyDst>;
Write DecodeWriteFlags( uint32_t v );

struct Texture
{
  ComPtr<ID3D12Resource>      Resource;
  ComPtr<D3D12MA::Allocation> Allocation;
  D3D12_RESOURCE_STATES       CurrentState;
  SRVHandle                   AsSRV;

  struct Desc
  {
    DXGI_FORMAT           Format;
    uint32_t              Width;
    uint32_t              Height;
    uint16_t              MipLevels{ 0 };
    uint16_t              ArraySize{ 1 };
    D3D12_RESOURCE_STATES InitState{ D3D12_RESOURCE_STATE_COMMON };
    D3D12_RESOURCE_FLAGS  Flags{ D3D12_RESOURCE_FLAG_NONE };
  };

  // ReSharper disable once CppInconsistentNaming
  void create( Desc const& desc, void* alloc );
  // ReSharper disable once CppInconsistentNaming
  void destroy( Desc const& desc, void* alloc );

  // ReSharper disable once CppInconsistentNaming
  void preRead( Desc const&, uint32_t flags, void* context );
  // ReSharper disable once CppInconsistentNaming
  void preWrite( Desc const&, uint32_t flags, void* context );
};

struct Buffer
{
  Ember::Buffer InnerBuffer;

  struct Desc
  {};

  // ReSharper disable once CppInconsistentNaming
  void create( Desc const&, void* )
  {
    UNIMPLEMENTED;
  }
  // ReSharper disable once CppInconsistentNaming
  void destroy( Desc const&, void* )
  {
    UNIMPLEMENTED;
  }
};

class Context
{
public:
  // Approx 1 second at 120 FPS
  uint64_t constexpr static kMaxAge = 120;

  struct FrameData
  {
    CommandList* CommandList;
  };

private:
  struct TexturePoolEntry
  {
    std::queue<Texture> Queue;
    uint64_t            TickStamp;

    bool                Empty() const;
    Texture             Pop();
    void                Push( Texture tex );
  };

  struct RenderTargets
  {
    std::vector<ID3D12Resource*>               Resources;
    std::vector<D3D12_RENDER_TARGET_VIEW_DESC> Descriptions;
    std::vector<LoadOperation>                 LoadOps;
  };

  struct DepthTargetEntry
  {
    ID3D12Resource*               Resource{ nullptr };
    D3D12_DEPTH_STENCIL_VIEW_DESC Desc;
    LoadOperation                 LoadOp;
  };

  using SRVCacheType = FlatMap<uint64_t, SRVHandle>;

  RenderDevice*                          m_RenderDevice;
  FrameData                              m_FrameData;
  FlatMap<uint64_t, TexturePoolEntry>    m_TransientTextures;
  FlatMap<ID3D12Resource*, SRVCacheType> m_TextureSRVCache;

  std::vector<CD3DX12_RESOURCE_BARRIER>  m_Barriers;
  RenderTargets                          m_CurrentRenderTargets;
  DepthTargetEntry                       m_CurrentDepthTarget;
  DirectX::XMUINT2                       m_RenderTargetSize;

  uint64_t                               m_TickCounter;
  uint32_t                               m_TextureCount;

  [[nodiscard]] Texture                  CreateTextureImpl( Texture::Desc const& desc ) const;
  void                                   FlushBarriers();

public:
  Context() = default;
  Context( RenderDevice* render_device );

  [[nodiscard]] RenderDevice*    GetRenderDevice() const;
  [[nodiscard]] FrameData const& GetFrameData() const;
  void                           SetFrameData( FrameData const& frame_data );

  void                           PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier );

  void                           SetRenderTarget(
                                uint32_t index, Texture const& render_target, Texture::Desc const& desc, bool as_srgb, LoadOperation load_op );
  void                  SetDepthTarget( Texture const& depth_target, Texture::Desc const& desc, LoadOperation load_op );

  void                  PreparePass();

  [[nodiscard]] Texture CreateTexture( Texture::Desc const& desc );
  void                  DestroyTexture( Texture::Desc const& desc, Texture tex );

  [[nodiscard]] uint32_t GetTextureCount() const;
  void                   Update();
  SRVHandle GetOrCreateSRVHandle( Texture const& texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& srv_desc );

  Context( Context const& other )                = delete;
  Context( Context&& other ) noexcept            = default;
  Context& operator=( Context const& other )     = delete;
  Context& operator=( Context&& other ) noexcept = default;
  ~Context();
};

} // namespace FG

} // namespace Ember
