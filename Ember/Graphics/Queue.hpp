#pragma once

#include <queue>

#include "CommandList.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"
#include "Util/ScopedHandle.hpp"

namespace Ember
{

class Queue
{
public:
  class Receipt
  {
    ID3D12Fence* m_Fence{ nullptr };
    uint64_t     m_FenceValue{ 0 };

  public:
    Receipt() = default;
    Receipt( ID3D12Fence* fence, uint64_t value );

    [[nodiscard]] bool         IsValid() const;
    [[nodiscard]] bool         IsComplete() const;

    [[nodiscard]] ID3D12Fence* GetFence() const;
    [[nodiscard]] uint64_t     GetFenceValue() const;
  };

private:
  struct InFlightAllocators
  {
    ComPtr<ID3D12CommandAllocator>  Allocator;
    uint64_t                        FenceValue;
    std::unique_ptr<ResourceBinder> Binder;
  };

  using CommandListQueue      = std::queue<ComPtr<ID3D12GraphicsCommandList7>>;
  using RTMQueue              = std::queue<std::unique_ptr<RenderTargetManager>>;
  using CommandAllocatorQueue = std::deque<InFlightAllocators>;
  using PoolAllocator         = std::unique_ptr<std::pmr::unsynchronized_pool_resource>;

  PoolAllocator              m_PoolAllocator;
  ComPtr<ID3D12Device2>      m_Device;
  ComPtr<ID3D12CommandQueue> m_CommandQueue;
  ComPtr<ID3D12Fence>        m_Fence;
  BindlessManager*           m_Bindless;
  CommandListQueue           m_CommandLists;
  RTMQueue                   m_RenderTargetManagers;
  CommandAllocatorQueue      m_CommandAllocators;
  ScopedHandle               m_FenceEvent;
  uint64_t                   m_FenceValue{ 0 };
  D3D12_COMMAND_LIST_TYPE    m_CommandListType{ D3D12_COMMAND_LIST_TYPE_DIRECT };

  Queue(
      ComPtr<ID3D12Device2>      device,
      BindlessManager*           bindless,
      ComPtr<ID3D12CommandQueue> command_queue,
      ComPtr<ID3D12Fence>        fence,
      ScopedHandle               fence_event,
      D3D12_COMMAND_LIST_TYPE    command_list_type );

  void ClearCompletedBinders() const;

public:
  Queue() = default;

  [[nodiscard]] ID3D12CommandQueue*     GetCommandQueue() const;
  [[nodiscard]] bool                    IsFenceComplete( uint64_t fence_value ) const;

  [[nodiscard]] Receipt                 CreateReceipt( uint64_t value = 0 ) const;
  [[nodiscard]] CommandList             GetCommandList();
  [[nodiscard]] D3D12_COMMAND_LIST_TYPE GetCommandListType() const noexcept;

  [[nodiscard]] Receipt                 Submit( CommandList&& command_list );
  [[nodiscard]] Receipt                 Signal();

  void                                  WaitOn( Receipt const& receipt ) const;
  void                                  QueueWaitOn( Receipt receipt ) const;
  void                                  WaitIdle();

  static void                           Create(
                                Queue* context, ComPtr<ID3D12Device2> device, BindlessManager* bindless, D3D12_COMMAND_LIST_TYPE type );

  Queue( Queue const& other )                = delete;
  Queue( Queue&& other ) noexcept            = default;
  Queue& operator=( Queue const& other )     = delete;
  Queue& operator=( Queue&& other ) noexcept = default;
  ~Queue();
};

} // namespace Ember
