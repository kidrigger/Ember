#pragma once

#include <deque>
#include <memory_resource>
#include <variant>

#include <Graphics/DeviceHandle.hpp>
#include <Util/DirectXHeaders.hpp>
#include <Util/Runtime.hpp>

namespace Ember
{
class RenderDevice;

class ResourceTracker
{
  using HandleVariant     = std::variant<CBVHandle, SRVHandle, UAVHandle, SamplerHandle>;
  using ResourceList      = std::pmr::deque<ComPtr<IUnknown>>;
  using CBVHandleList     = std::pmr::deque<CBVHandle>;
  using SRVHandleList     = std::pmr::deque<SRVHandle>;
  using UAVHandleList     = std::pmr::deque<UAVHandle>;
  using SamplerHandleList = std::pmr::deque<SamplerHandle>;
  using BarrierList       = std::pmr::deque<CD3DX12_RESOURCE_BARRIER>;

  RenderDevice*     m_Device;
  ResourceList      m_Resources;
  CBVHandleList     m_CBVHandles;
  SRVHandleList     m_SRVHandles;
  UAVHandleList     m_UAVHandles;
  SamplerHandleList m_SamplerHandles;
  BarrierList       m_Barriers;

public:
  ResourceTracker() = default;

  ResourceTracker( RenderDevice* device, std::pmr::polymorphic_allocator<> const& allocator );

  void PushResource( ComPtr<IUnknown> resource );
  void PushHandle( CBVHandle handle );
  void PushHandle( SRVHandle handle );
  void PushHandle( UAVHandle handle );
  void PushHandle( SamplerHandle handle );
  void PushBarrier( CD3DX12_RESOURCE_BARRIER const& barrier );
  void Clear( std::vector<D3D12_RESOURCE_BARRIER>* pending_barriers );

  //
  [[nodiscard]] bool IsEmpty() const;
};

} // namespace Ember
