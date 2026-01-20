#include "TexturePool.hpp"

#include "FrameGraphHelper.hpp"

bool Ember::TexturePool::TexturePoolEntry::Empty() const
{
  return Queue.empty();
}

Ember::Texture Ember::TexturePool::TexturePoolEntry::Pop()
{
  Texture tex = Queue.front();
  Queue.pop();

  return tex;
}

void Ember::TexturePool::TexturePoolEntry::Push( Texture tex )
{
  Queue.push( std::move( tex ) );
}

Ember::TexturePool::TexturePool( RenderDevice* render_device )
  : m_RenderDevice{ render_device }, m_TickCounter{ 0 }, m_TextureCount{ 0 }
{}

Ember::Texture Ember::TexturePool::CreateTexture( Texture::Desc const& desc )
{
  uint64_t const hash      = desc.Hash();

  auto           it        = m_TransientTextures.Find( hash );
  auto&          res_queue = it == m_TransientTextures.end() ? m_TransientTextures.Put( hash, {} ) : it->second;

  res_queue.TickStamp      = m_TickCounter;

  if ( not res_queue.Empty() )
  {
    return res_queue.Pop();
  }

  m_TextureCount++;
  return m_RenderDevice->CreateTexture( desc );
}

void Ember::TexturePool::DestroyTexture( Texture::Desc const& desc, Texture tex )
{
  uint64_t const hash = desc.Hash();

  auto           it   = m_TransientTextures.Find( hash );
  if ( it == m_TransientTextures.end() ) return;

  it->second.Push( std::move( tex ) );
}

uint32_t Ember::TexturePool::GetTextureCount() const
{
  return m_TextureCount;
}

void Ember::TexturePool::Update()
{
  m_TickCounter++;

  m_TransientTextures.EraseIf(
      [&]( uint64_t const&, TexturePoolEntry const& val )
      {
        bool const marked_del = val.Empty() or m_TickCounter - val.TickStamp >= kMaxAge;
        if ( marked_del ) m_TextureCount -= ( uint32_t )val.Queue.size();
        return marked_del;
      } );
}
