#pragma once

#include <queue>
#include <variant>

#include "DeviceHandle.hpp"
#include "RenderPassCommon.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
class RenderTargetManager;
class RenderDevice;

namespace FG
{

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

struct Attachment
{
  WriteType Type  = WriteType::kRTV; // 2 bits
  uint8_t   Index = 0xFF;            // 3 bits

  operator uint32_t() const;
  static Attachment Decode( uint32_t flag );
};

struct DepthStencil
{
  WriteType Type = WriteType::kDSV; // 2 bits

  operator uint32_t() const;
  static DepthStencil Decode( uint32_t flag );
};

struct CopyDst
{
  WriteType Type = WriteType::kCopy; // 2 bits

  operator uint32_t() const;
  static CopyDst Decode( uint32_t flag );
};

using Read = std::variant<std::monostate, ShaderResource, std::monostate, CopySrc>;
Read DecodeReadFlags( uint32_t v );

using Write = std::variant<Attachment, DepthStencil, CopyDst>;
Write DecodeWriteFlags( uint32_t v );

struct Texture
{
  ComPtr<ID3D12Resource>       Resource;
  ComPtr<D3D12MA::Allocation>  Allocation;
  D3D12_RESOURCE_STATES        CurrentState;
  FlatMap<uint64_t, SRVHandle> SRVHandleCache;
  SRVHandle                    AsSRV;

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

class Context
{
public:
  // Approx 1 second at 120 FPS
  uint64_t constexpr static kMaxAge = 120;

  struct FrameData
  {
    ID3D12GraphicsCommandList6* CommandList;
    uint32_t                    Width;
    uint32_t                    Height;
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

  RenderDevice*                         m_RenderDevice;
  std::unique_ptr<RenderTargetManager>  m_RenderTargetManager;
  FrameData                             m_FrameData;
  FlatMap<uint64_t, TexturePoolEntry>   m_Textures;
  std::vector<CD3DX12_RESOURCE_BARRIER> m_Barriers;
  uint64_t                              m_TickCounter;
  uint32_t                              m_TextureCount;

  [[nodiscard]] Texture                 CreateTextureImpl( Texture::Desc const& desc ) const;

public:
  Context() = default;
  Context( RenderDevice* render_device, std::unique_ptr<RenderTargetManager> render_target_manager );

  static void                        Create( Context* out, RenderDevice* render_device );

  [[nodiscard]] RenderDevice*        GetRenderDevice() const;
  [[nodiscard]] FrameData const&     GetFrameData() const;
  void                               SetFrameData( FrameData const& frame_data );
  [[nodiscard]] RenderTargetManager* GetRenderTargetManager() const;

  void                               PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier );
  void                               FlushBarriers();

  [[nodiscard]] Texture              CreateTexture( Texture::Desc const& desc );
  void                               DestroyTexture( Texture::Desc const& desc, Texture tex );

  [[nodiscard]] uint32_t             GetTextureCount() const;
  void                               Update();
};

} // namespace FG

} // namespace Ember
