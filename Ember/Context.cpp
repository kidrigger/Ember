#include "Context.hpp"

#include "Util/HelperUtils.hpp"

Ember::Context::Receipt::Receipt( ID3D12Fence* fence, uint64_t const value ) : m_Fence{ fence }, m_FenceValue{ value }
{}

bool Ember::Context::Receipt::IsValid() const
{
  return m_Fence;
}

bool Ember::Context::Receipt::IsComplete() const
{
  ASSERT( m_Fence );
  return m_Fence->GetCompletedValue() >= m_FenceValue;
}

ID3D12Fence* Ember::Context::Receipt::GetFence() const
{
  ASSERT( m_Fence );
  return m_Fence;
}

uint64_t Ember::Context::Receipt::GetFenceValue() const
{
  ASSERT( m_Fence );
  return m_FenceValue;
}

Ember::Context::Context(
    ComPtr<ID3D12Device2>         device,
    ComPtr<ID3D12CommandQueue>    command_queue,
    ComPtr<ID3D12Fence>           fence,
    ScopedHandle                  fence_event,
    D3D12_COMMAND_LIST_TYPE const command_list_type )
  : m_Device{ std::move( device ) }
  , m_CommandQueue{ std::move( command_queue ) }
  , m_Fence{ std::move( fence ) }
  , m_FenceEvent{ std::move( fence_event ) }
  , m_CommandListType{ command_list_type }
{}

ID3D12CommandQueue* Ember::Context::GetCommandQueue() const
{
  return m_CommandQueue.Get();
}

bool Ember::Context::IsFenceComplete( uint64_t const fence_value ) const
{
  return m_Fence->GetCompletedValue() >= fence_value;
}

Ember::Context::Receipt Ember::Context::CreateReceipt( uint64_t value ) const
{
  return { m_Fence.Get(), value };
}

Ember::Context::CommandList Ember::Context::GetCommandList()
{
  ComPtr<ID3D12CommandAllocator> command_allocator;
  if ( not m_CommandAllocators.empty() and IsFenceComplete( m_CommandAllocators.front().FenceValue ) )
  {
    // Available free command allocator.
    command_allocator = m_CommandAllocators.front().Allocator;
    m_CommandAllocators.pop();
    ERR_ABORT( command_allocator->Reset() );
  }
  else
  {
    ERR_ABORT( m_Device->CreateCommandAllocator( m_CommandListType, IID_PPV_ARGS( &command_allocator ) ) );
  }

  CommandList command_list;
  if ( m_CommandLists.empty() )
  {
    ERR_ABORT( m_Device->CreateCommandList(
        0, m_CommandListType, command_allocator.Get(), nullptr, IID_PPV_ARGS( &command_list ) ) );
    ERR_ABORT( command_list->SetPrivateDataInterface( _uuidof( ID3D12CommandAllocator ), command_allocator.Get() ) );

    return command_list;
  }

  command_list = m_CommandLists.front();
  m_CommandLists.pop();

  ERR_ABORT( command_list->Reset( command_allocator.Get(), nullptr ) );
  ERR_ABORT( command_list->SetPrivateDataInterface( _uuidof( ID3D12CommandAllocator ), command_allocator.Get() ) );

  return command_list;
}

Ember::Context::Receipt Ember::Context::Submit( CommandList&& command_list )
{
  ID3D12CommandList* p_command_list = command_list.Get();
  m_CommandQueue->ExecuteCommandLists( 1, &p_command_list );
  uint64_t const signal_value = ++m_FenceValue;
  ERR_ABORT( m_CommandQueue->Signal( m_Fence.Get(), signal_value ) );
  ComPtr<ID3D12CommandAllocator> command_allocator;
  UINT                           data_size = sizeof( ID3D12CommandAllocator* );
  ERR_ABORT(
      command_list->GetPrivateData( _uuidof( ID3D12CommandAllocator ), &data_size, command_allocator.GetAddressOf() ) );
  ERR_ABORT( command_list->SetPrivateDataInterface( _uuidof( ID3D12CommandAllocator ), nullptr ) );

  m_CommandLists.emplace( std::move( command_list ) );
  m_CommandAllocators.emplace( command_allocator, signal_value );

  return { m_Fence.Get(), signal_value };
}

Ember::Context::Receipt Ember::Context::Signal()
{
  auto const wait_on_fence_value = ++m_FenceValue;

  // Signal on commandQueue.
  ERR_ABORT( m_CommandQueue->Signal( m_Fence.Get(), wait_on_fence_value ) );

  return Receipt{ m_Fence.Get(), wait_on_fence_value };
}

void Ember::Context::WaitOn( Receipt const& receipt ) const
{
  if ( not receipt.IsValid() or receipt.IsComplete() ) return;

  // Set the event on fence.
  ERR_ABORT( receipt.GetFence()->SetEventOnCompletion( receipt.GetFenceValue(), m_FenceEvent ) );

  // Wait for the event (with max timeout).
  ::WaitForSingleObject( m_FenceEvent, INFINITE );
}

void Ember::Context::QueueWaitOn( Receipt const receipt ) const
{
  if ( receipt.IsComplete() ) return;

  ERR_ABORT( m_CommandQueue->Wait( receipt.GetFence(), receipt.GetFenceValue() ) );
}

void Ember::Context::WaitIdle()
{
  WaitOn( Signal() );
}

void Ember::Context::Create( Context* context, ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE const type )
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
    assert( fence_event && "Failed to create fence event" );
  }

  new ( context ) Context{ std::move( device ), std::move( command_queue ), std::move( fence ), fence_event, type };
}

Ember::Context::~Context()
{
  if ( m_CommandQueue ) WaitIdle();
}
