#pragma once

#include <forward_list>
#include <variant>
#include "Buffer.hpp"
#include "DeviceHandle.hpp"
#include "Texture.hpp"
#include "Util/FlatMap.hpp"

namespace Ember
{
class RenderDevice;

class ResourceBinder
{
  using VarHandle = std::variant<SRVHandle, UAVHandle, CBVHandle>;
  BindlessManager*                m_Bindless;
  std::pmr::forward_list<Texture> m_UsedTextures;
  std::pmr::forward_list<Buffer>  m_UsedBuffers;
  FlatMap<uint64_t, VarHandle>    m_TempHandles;

public:
  ResourceBinder() = default;
  ResourceBinder( BindlessManager* bindless, std::pmr::polymorphic_allocator<> const& allocator );

  CBVHandle BindCBV( Buffer buffer ) noexcept;
  SRVHandle BindSRV( Buffer buffer ) noexcept;
  UAVHandle BindUAV( Buffer buffer ) noexcept;
  SRVHandle BindSRV( Texture texture ) noexcept;
  UAVHandle BindUAV( Texture texture ) noexcept;
  SRVHandle BindTransient( Texture texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept;
  UAVHandle BindTransient( Texture texture, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept;

  void      Clear();
  ~ResourceBinder();
};

template <typename T>
concept BindableStructure = requires( ResourceBinder* binder, T const& value ) {
  typename T::BoundDataType;
  std::is_trivially_copyable_v<typename T::BoundDataType>;
  { value.Bind( binder ) } -> std::same_as<typename T::BoundDataType>;
};

} // namespace Ember
