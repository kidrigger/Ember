#pragma once

#include <forward_list>
#include <set>
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
  std::pmr::set<ComPtr<IUnknown>> m_UsedResources;
  FlatMap<uint64_t, VarHandle>    m_TempHandles;

public:
  ResourceBinder() = default;
  ResourceBinder( BindlessManager* bindless, std::pmr::polymorphic_allocator<> const& resource );

  CBVHandle BindCBV( Buffer const& buffer ) noexcept;
  SRVHandle BindSRV( Buffer const& buffer ) noexcept;
  UAVHandle BindUAV( Buffer const& buffer ) noexcept;
  SRVHandle BindSRV( Texture const& texture ) noexcept;
  UAVHandle BindUAV( Texture const& texture ) noexcept;
  SRVHandle BindTransient( Texture const& texture, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept;
  UAVHandle BindTransient( Texture const& texture, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept;
  SRVHandle BindTransient( ComPtr<ID3D12Resource> resource, CD3DX12_SHADER_RESOURCE_VIEW_DESC const& desc ) noexcept;
  UAVHandle BindTransient( ComPtr<ID3D12Resource> resource, CD3DX12_UNORDERED_ACCESS_VIEW_DESC const& desc ) noexcept;
  void      Track( ComPtr<IUnknown> resource ) noexcept;

  void      Clear();
  ~ResourceBinder();
};

template <typename T>
concept BindableStructure = requires( ResourceBinder* binder, T const& value ) {
  typename T::BoundDataType;
  std::is_trivially_copyable_v<typename T::BoundDataType>;
  { value.Bind( binder ) } -> std::same_as<typename T::BoundDataType>;
};

template <typename TDesc>
struct BindTransient
{
  ComPtr<ID3D12Resource> Resource;
  TDesc                  Desc;

  BindTransient( ComPtr<ID3D12Resource> resource, TDesc const& desc ) : Resource( resource ), Desc( desc )
  {}

  using BoundDataType = decltype( ( ( ResourceBinder* )0 )->BindTransient( Resource, Desc ) );

  BoundDataType Bind( ResourceBinder* binder ) const
  {
    return binder->BindTransient( Resource, Desc );
  }
};

static_assert( BindableStructure<BindTransient<CD3DX12_SHADER_RESOURCE_VIEW_DESC>> );

struct BindTransientUAV
{
  ComPtr<ID3D12Resource>             Resource;
  CD3DX12_UNORDERED_ACCESS_VIEW_DESC Desc;

  using BoundDataType = UAVHandle;

  BoundDataType Bind( ResourceBinder* binder ) const
  {
    return binder->BindTransient( Resource, Desc );
  }
};

template <typename TResource>
struct BindSRV
{
  TResource Resource;

  BindSRV( TResource resource ) : Resource( resource )
  {}

  using BoundDataType = SRVHandle;

  BoundDataType Bind( ResourceBinder* binder ) const
  {
    return binder->BindSRV( Resource );
  }
};

template <typename TResource>
struct BindUAV
{
  TResource Resource;

  BindUAV( TResource resource ) : Resource( resource )
  {}

  using BoundDataType = UAVHandle;

  BoundDataType Bind( ResourceBinder* binder ) const
  {
    return binder->BindUAV( Resource );
  }
};

} // namespace Ember
