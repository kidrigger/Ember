#pragma once

#include <queue>

#include <Graphics/Texture.hpp>
#include <Util/FlatMap.hpp>

namespace Ember
{
class RenderDevice;

// Transient texture pool.
class TexturePool
{
public:
  // Approx 1 second at 120 FPS
  uint64_t constexpr static kMaxAge = 120;

private:
  struct TexturePoolEntry
  {
    std::queue<Texture>   Queue;
    uint64_t              TickStamp;

    [[nodiscard]] bool    Empty() const;
    [[nodiscard]] Texture Pop();
    void                  Push( Texture tex );
  };

  RenderDevice*                       m_RenderDevice;
  FlatMap<uint64_t, TexturePoolEntry> m_TransientTextures;
  uint64_t                            m_TickCounter;
  uint32_t                            m_TextureCount;

public:
  TexturePool() = default;
  explicit TexturePool( RenderDevice* render_device );

  [[nodiscard]] Texture  CreateTexture( Texture::Desc const& desc );
  void                   DestroyTexture( Texture::Desc const& desc, Texture tex );

  [[nodiscard]] uint32_t GetTextureCount() const;
  void                   Update();
};

} // namespace Ember
