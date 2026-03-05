#include "Queue.hpp"

#include "RenderTargetManager.hpp"
#include "Util/HelperUtils.hpp"

Ember::Queue::Receipt::Receipt( ID3D12Fence* fence, uint64_t const value ) : m_Fence{ fence }, m_FenceValue{ value }
{}

bool Ember::Queue::Receipt::IsValid() const
{
  return m_Fence;
}

bool Ember::Queue::Receipt::IsComplete() const
{
  ASSERT( m_Fence );
  return m_Fence->GetCompletedValue() >= m_FenceValue;
}

ID3D12Fence* Ember::Queue::Receipt::GetFence() const
{
  ASSERT( m_Fence );
  return m_Fence;
}

uint64_t Ember::Queue::Receipt::GetFenceValue() const
{
  ASSERT( m_Fence );
  return m_FenceValue;
}

Ember::Queue::Queue(
    ComPtr<ID3D12Device2>         device,
    BindlessManager*              bindless,
    ComPtr<ID3D12CommandQueue>    command_queue,
    ComPtr<ID3D12Fence>           fence,
    ScopedHandle                  fence_event,
    D3D12_COMMAND_LIST_TYPE const command_list_type )
  : m_PoolAllocator{ std::make_unique<std::pmr::unsynchronized_pool_resource>() }
  , m_Device{ std::move( device ) }
  , m_CommandQueue{ std::move( command_queue ) }
  , m_Fence{ std::move( fence ) }
  , m_Bindless{ bindless }
  , m_FenceEvent{ std::move( fence_event ) }
  , m_CommandListType{ command_list_type }
{}

void Ember::Queue::ClearCompletedBinders() const
{
  auto const current_value = m_Fence->GetCompletedValue();
  for ( auto& bind : m_CommandAllocators )
  {
    if ( current_value < bind.FenceValue ) continue;

    bind.Binder->Clear();
  }
}

ID3D12CommandQueue* Ember::Queue::GetCommandQueue() const
{
  return m_CommandQueue.Get();
}

bool Ember::Queue::IsFenceComplete( uint64_t const fence_value ) const
{
  return m_Fence->GetCompletedValue() >= fence_value;
}

Ember::Queue::Receipt Ember::Queue::CreateReceipt( uint64_t value ) const
{
  return { m_Fence.Get(), value };
}

Ember::CommandList Ember::Queue::GetCommandList()
{
  std::unique_ptr<ResourceBinder>      binder;
  ComPtr<ID3D12CommandAllocator>       command_allocator;
  std::unique_ptr<RenderTargetManager> rtm;
  ComPtr<ID3D12GraphicsCommandList7>   command_list;

  if ( not m_CommandAllocators.empty() and IsFenceComplete( m_CommandAllocators.front().FenceValue ) )
  {
    // Available free command allocator.
    command_allocator = std::move( m_CommandAllocators.front().Allocator );
    binder            = std::move( m_CommandAllocators.front().Binder );
    m_CommandAllocators.pop_front();
    ERR_ABORT( command_allocator->Reset() );
    binder->Clear();
  }
  else
  {
    ERR_ABORT( m_Device->CreateCommandAllocator( m_CommandListType, IID_PPV_ARGS( &command_allocator ) ) );
    binder = std::make_unique<ResourceBinder>( m_Bindless, std::pmr::polymorphic_allocator<>{ m_PoolAllocator.get() } );
  }

  if ( m_CommandLists.empty() )
  {
    // D3D CommandList and RTM are added and removed in lock-step.
    ERR_ABORT( m_Device->CreateCommandList(
        0, m_CommandListType, command_allocator.Get(), nullptr, IID_PPV_ARGS( &command_list ) ) );

    rtm = std::make_unique_for_overwrite<RenderTargetManager>();
    RenderTargetManager::Create( rtm.get(), m_Device );
  }
  else
  {
    command_list = m_CommandLists.front();
    m_CommandLists.pop();
    ERR_ABORT( command_list->Reset( command_allocator.Get(), nullptr ) );

    rtm = std::move( m_RenderTargetManagers.front() );
    m_RenderTargetManagers.pop();
  }

  auto desc_heaps = m_Bindless->GetBindlessDescriptorHeaps();
  command_list->SetDescriptorHeaps( CountOf( desc_heaps ), DataOf( desc_heaps ) );

  return CommandList{
    std::move( command_list ),
    std::move( command_allocator ),
    std::move( rtm ),
    std::move( binder ),
  };
}

[[nodiscard]] D3D12_COMMAND_LIST_TYPE Ember::Queue::GetCommandListType() const noexcept
{
  return m_CommandListType;
}

Ember::Queue::Receipt Ember::Queue::Submit( CommandList&& command_list )
{
  ERR_ABORT( command_list.Close() );
  ID3D12CommandList* p_command_list = command_list.Get();

  m_CommandQueue->ExecuteCommandLists( 1, &p_command_list );
  uint64_t const signal_value = ++m_FenceValue;
  ERR_ABORT( m_CommandQueue->Signal( m_Fence.Get(), signal_value ) );

  auto [gfx_command_list, command_allocator, rtm, bindless] = command_list.Release();
  m_CommandLists.emplace( std::move( gfx_command_list ) );
  m_CommandAllocators.emplace_back( std::move( command_allocator ), signal_value, std::move( bindless ) );
  m_RenderTargetManagers.emplace( std::move( rtm ) );

  return { m_Fence.Get(), signal_value };
}

Ember::Queue::Receipt Ember::Queue::Signal()
{
  auto const wait_on_fence_value = ++m_FenceValue;

  // Signal on commandQueue.
  ERR_ABORT( m_CommandQueue->Signal( m_Fence.Get(), wait_on_fence_value ) );

  return Receipt{ m_Fence.Get(), wait_on_fence_value };
}

void Ember::Queue::WaitOn( Receipt const& receipt ) const
{
  if ( not receipt.IsValid() or receipt.IsComplete() )
  {
    ClearCompletedBinders();
    return;
  }

  // Set the event on fence.
  ERR_ABORT( receipt.GetFence()->SetEventOnCompletion( receipt.GetFenceValue(), m_FenceEvent ) );

  // Wait for the event (with max timeout).
  ::WaitForSingleObject( m_FenceEvent, INFINITE );

  ClearCompletedBinders();
}

void Ember::Queue::QueueWaitOn( Receipt const receipt ) const
{
  if ( receipt.IsComplete() ) return;

  ERR_ABORT( m_CommandQueue->Wait( receipt.GetFence(), receipt.GetFenceValue() ) );
}

void Ember::Queue::WaitIdle()
{
  WaitOn( Signal() );
}

void Ember::Queue::Create(
    Queue* context, ComPtr<ID3D12Device2> device, BindlessManager* bindless, D3D12_COMMAND_LIST_TYPE const type )
{
  D3D12_COMMAND_QUEUE_DESC const desc = {
    .Type     = type,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
  };

  ComPtr<ID3D12CommandQueue> command_queue;
  ERR_ABORT( device->CreateCommandQueue( &desc, IID_PPV_ARGS( &command_queue ) ) );

  ComPtr<ID3D12Fence> fence;
  ERR_ABORT( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &fence ) ) );

  HANDLE fence_event;
  {
    fence_event = ::CreateEventA( nullptr, FALSE, FALSE, nullptr );
    ASSERT_M( fence_event, "Failed to create fence event" );
  }

  new ( context ) Queue{
    std::move( device ), bindless, std::move( command_queue ), std::move( fence ), fence_event, type,
  };
}

Ember::Queue::~Queue()
{
  if ( m_CommandQueue ) WaitIdle();
}
