#pragma once

#include <queue>

#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"
#include "Util/ScopedHandle.hpp"

namespace Ember
{

class Context
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

  using CommandList = ComPtr<ID3D12GraphicsCommandList>;

private:
  struct InFlightAllocators
  {
    ComPtr<ID3D12CommandAllocator> Allocator;
    uint64_t                       FenceValue;
  };

  using CommandListQueue      = std::queue<CommandList>;
  using CommandAllocatorQueue = std::queue<InFlightAllocators>;

  ComPtr<ID3D12Device2>      m_Device;
  ComPtr<ID3D12CommandQueue> m_CommandQueue;
  ComPtr<ID3D12Fence>        m_Fence;
  CommandListQueue           m_CommandLists;
  CommandAllocatorQueue      m_CommandAllocators;
  ScopedHandle               m_FenceEvent;
  uint64_t                   m_FenceValue{ 0 };
  D3D12_COMMAND_LIST_TYPE    m_CommandListType;

public:
  Context() = default;

  Context(
      ComPtr<ID3D12Device2>      device,
      ComPtr<ID3D12CommandQueue> command_queue,
      ComPtr<ID3D12Fence>        fence,
      ScopedHandle               fence_event,
      D3D12_COMMAND_LIST_TYPE    command_list_type );

  [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const;
  [[nodiscard]] bool                IsFenceComplete( uint64_t fence_value ) const;

  [[nodiscard]] Receipt             CreateReceipt( uint64_t value = 0 ) const;
  CommandList                       GetCommandList();

  Receipt                           Submit( CommandList&& command_list );
  [[nodiscard]] Receipt             Signal();

  void                              WaitOn( Receipt const& receipt ) const;
  void                              QueueWaitOn( Receipt receipt ) const;
  void                              WaitIdle();

  static void Create( Context* context, ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type );

  Context( Context const& other )                = delete;
  Context( Context&& other ) noexcept            = default;
  Context& operator=( Context const& other )     = delete;
  Context& operator=( Context&& other ) noexcept = default;
  ~Context()                                     = default;
};

} // namespace Ember
