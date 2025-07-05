#include "RenderDevice.hpp"

#include <span>

#include "BufferManager.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "D3DCompiler.lib" )
#pragma comment( lib, "dxgi.lib" )

void UpdateRenderTargetViews(
    ComPtr<ID3D12Device> const&              device,
    ComPtr<IDXGISwapChain4> const&           swapchain,
    ComPtr<ID3D12DescriptorHeap> const&      rtv_descriptor_heap,
    UINT                                     rtv_descriptor_size,
    std::span<ComPtr<ID3D12Resource>> const& backbuffers );

Ember::RenderDevice::RenderDevice(
    ComPtr<ID3D12Device2> const&                  device,
    ComPtr<D3D12MA::Allocator> const&             allocator,
    ComPtr<ID3D12CommandQueue> const&             direct_queue,
    UINT32 const                                  swapchain_width,
    UINT32 const                                  swapchain_height,
    ComPtr<IDXGISwapChain4> const&                swapchain,
    std::vector<ComPtr<ID3D12Resource>>&&         backbuffers,
    ComPtr<ID3D12DescriptorHeap> const&           rtv_descriptor_heap,
    UINT32 const                                  rtv_descriptor_size,
    UINT32 const                                  current_backbuffer_index,
    ComPtr<ID3D12GraphicsCommandList> const&      command_list,
    std::vector<ComPtr<ID3D12CommandAllocator>>&& command_allocators,
    ComPtr<ID3D12Fence> const&                    fence,
    HANDLE const                                  fence_event,
    bool const                                    is_tearing_supported,
    BufferManager&&                               buffer_manager )
  : m_Device{ device }
  , m_Allocator{ allocator }
  , m_DirectQueue{ direct_queue }
  , m_SwapchainWidth{ swapchain_width }
  , m_SwapchainHeight{ swapchain_height }
  , m_Swapchain{ swapchain }
  , m_Backbuffers{ std::move( backbuffers ) }
  , m_RTVDescriptorHeap{ rtv_descriptor_heap }
  , m_RTVDescriptorSize{ rtv_descriptor_size }
  , m_CurrentBackbufferIndex{ current_backbuffer_index }
  , m_CommandList{ command_list }
  , m_CommandAllocators{ std::move( command_allocators ) }
  , m_Fence{ fence }
  , m_FenceEvent{ fence_event }
  , m_BufferManager{ std::move( buffer_manager ) }
{
  m_FenceValues.resize( kNumFrames, 0 );
  if ( is_tearing_supported )
  {
    m_VsyncAndTearing = m_VsyncAndTearing | kSupportTearingBit;
  }
}

ComPtr<ID3D12Device2> Ember::RenderDevice::GetDevice()
{
  return m_Device;
}

Ember::RenderDevice Ember::RenderDevice::Create( HWND window_handle, bool const use_warp )
{
#if defined( _DEBUG )
  {
    ComPtr<ID3D12Debug> debug_interface;
    ERR_ABORT( D3D12GetDebugInterface( IID_PPV_ARGS( &debug_interface ) ) );
    debug_interface->EnableDebugLayer();
  }
#endif

  // Create Factory, this will be used for everything.
  ComPtr<IDXGIFactory4> dxgi_factory;
  {
    UINT create_factory_flags = 0;
#if defined( _DEBUG )
    create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ERR_ABORT( CreateDXGIFactory2( create_factory_flags, IID_PPV_ARGS( &dxgi_factory ) ) );
  }

  bool is_tearing_supported = false;
  {
    BOOL                  allow_tearing = FALSE;

    ComPtr<IDXGIFactory5> factory5;
    if ( SUCCEEDED( dxgi_factory.As( &factory5 ) ) )
    {
      if ( FAILED( factory5->CheckFeatureSupport(
               DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof( allow_tearing ) ) ) )
      {
        allow_tearing = FALSE;
      }
    }

    is_tearing_supported = ( allow_tearing == TRUE );
  }

  // The main adapter for a given device. We need to find the relevant adapter.
  ComPtr<IDXGIAdapter4> adapter;
  {
    // All the functions return adapter1 interface;
    // The underlying adapter is actually adapter4, so we can do the cast.
    ComPtr<IDXGIAdapter1> adapter1;

    if ( use_warp )
    {
      ERR_ABORT( dxgi_factory->EnumWarpAdapter( IID_PPV_ARGS( &adapter1 ) ) );
      ERR_ABORT( adapter1.As( &adapter ) );
    }
    else
    {
      SIZE_T max_dedicated_video_memory = 0;
      for ( UINT i = 0; dxgi_factory->EnumAdapters1( i, &adapter1 ) != DXGI_ERROR_NOT_FOUND; ++i )
      {
        DXGI_ADAPTER_DESC1 adapter_desc;
        ERR_ABORT( adapter1->GetDesc1( &adapter_desc ) );

        if ( ( adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) == 0 and
             // UUID but nullptr avoids creating a device. Effectively testing if it is possible to get D3D12_2
             SUCCEEDED(
                 D3D12CreateDevice( adapter1.Get(), D3D_FEATURE_LEVEL_12_2, __uuidof( ID3D12Device ), nullptr ) ) and
             adapter_desc.DedicatedVideoMemory > max_dedicated_video_memory )
        {
          max_dedicated_video_memory = adapter_desc.DedicatedVideoMemory;
          ERR_ABORT( adapter1.As( &adapter ) );
        }
      }
    }
  }

  ComPtr<ID3D12Device2> device;
  // Fetch device.
  {
    ERR_ABORT( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS( &device ) ) );

#if defined( _DEBUG )
    // Setup InfoQueue on debug (this is similar to a debug messenger in vulkan).
    ComPtr<ID3D12InfoQueue> info_queue;
    if ( SUCCEEDED( device.As( &info_queue ) ) )
    {
      ERR_ABORT( info_queue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, true ) );
      ERR_ABORT( info_queue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, true ) );
      ERR_ABORT( info_queue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_WARNING, true ) );

      D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

      D3D12_MESSAGE_ID       deny_ids[]   = {
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
        D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
        D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
      };

      D3D12_INFO_QUEUE_FILTER new_filter = {
        .DenyList = {
          .NumSeverities = COUNTOF(severities),
          .pSeverityList = severities,
          .NumIDs = COUNTOF(deny_ids),
          .pIDList = deny_ids,
        },
      };

      ERR_ABORT( info_queue->PushStorageFilter( &new_filter ) );
    }
#endif
  }

  ComPtr<D3D12MA::Allocator> allocator;
  {
    D3D12MA::ALLOCATOR_DESC allocator_desc = {
      .Flags    = D3D12MA::ALLOCATOR_FLAG_NONE,
      .pDevice  = device.Get(),
      .pAdapter = adapter.Get(),
    };

    ERR_ABORT( CreateAllocator( &allocator_desc, allocator.GetAddressOf() ) );
  }

  // Command Queue Creation.
  ComPtr<ID3D12CommandQueue> command_queue;
  {
    D3D12_COMMAND_QUEUE_DESC desc = {
      .Type     = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0,
    };

    ERR_ABORT( device->CreateCommandQueue( &desc, IID_PPV_ARGS( &command_queue ) ) );
  }

  RECT window_rect;
  ::GetWindowRect( window_handle, &window_rect );
  uint32_t const width  = window_rect.right - window_rect.left;
  uint32_t const height = window_rect.bottom - window_rect.top;

  // Swapchain Creation
  ComPtr<IDXGISwapChain4> swapchain;
  {
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
      .Width       = width,
      .Height      = height,
      .Format      = DXGI_FORMAT_R8G8B8A8_UNORM,
      .Stereo      = FALSE,
      .SampleDesc  = { 1, 0 },
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = kNumFrames,
      .Scaling     = DXGI_SCALING_STRETCH,
      .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED,
      .Flags       = is_tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : static_cast<UINT>( 0 ),
    };

    ComPtr<IDXGISwapChain1> swapchain1;
    ERR_ABORT( dxgi_factory->CreateSwapChainForHwnd(
        command_queue.Get(), window_handle, &swapchain_desc, nullptr, nullptr, &swapchain1 ) );

    ERR_ABORT( swapchain1.As( &swapchain ) );
  }

  UINT current_backbuffer_index = swapchain->GetCurrentBackBufferIndex();

  // Create DescriptorHeap
  ComPtr<ID3D12DescriptorHeap>        rtv_descriptor_heap;
  UINT                                rtv_descriptor_size;
  std::vector<ComPtr<ID3D12Resource>> backbuffers( kNumFrames );
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = kNumFrames,
    };

    ERR_ABORT( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &rtv_descriptor_heap ) ) );
    rtv_descriptor_size = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

    UpdateRenderTargetViews( device, swapchain, rtv_descriptor_heap, rtv_descriptor_size, backbuffers );
  }

  // Create Command Allocator
  std::vector<ComPtr<ID3D12CommandAllocator>> command_allocators( kNumFrames );
  for ( int i = 0; i < kNumFrames; ++i )
  {
    ERR_ABORT(
        device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &command_allocators[i] ) ) );
  }

  // Create a CommandList (for the current frame)
  // Unlike Vulkan CommandBuffers, the lists themselves are 'reusable' after submit.
  // Only the allocators need to be 'valid'.
  ComPtr<ID3D12GraphicsCommandList> command_list;
  {
    ERR_ABORT( device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        command_allocators[current_backbuffer_index].Get(),
        nullptr,
        IID_PPV_ARGS( &command_list ) ) );

    // We don't record anything. Just clear the screen.
    ERR_ABORT( command_list->Close() );
  }

  // Create a Fence (this is similar to timeline semaphores on Vulkan)
  ComPtr<ID3D12Fence> fence;
  {
    ERR_ABORT( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &fence ) ) );
  }

  // Create Event handle.
  // These are OS events that block CPU threads.
  // Benefit of DirectX integrating pretty tightly into Windows.
  HANDLE fence_event;
  {
    fence_event = ::CreateEventA( nullptr, FALSE, FALSE, nullptr );
    assert( fence_event && "Failed to create fence event" );
  }

  BufferManager buffer_manager = BufferManager::Create( 1000 );

  return RenderDevice{
    device,
    allocator,
    command_queue,
    width,
    height,
    swapchain,
    std::move( backbuffers ),
    rtv_descriptor_heap,
    rtv_descriptor_size,
    current_backbuffer_index,
    command_list,
    std::move( command_allocators ),
    fence,
    fence_event,
    is_tearing_supported,
    std::move( buffer_manager ),
  };
}

void Ember::RenderDevice::Destroy()
{
  if ( IsInit() )
  {
    WaitIdle();

    m_BufferManager.Destroy();

    ::CloseHandle( m_FenceEvent );
    m_FenceEvent = nullptr;
  }
}

void Ember::RenderDevice::ResizeSwapchain( uint32_t const width, uint32_t const height )
{
  if ( m_SwapchainHeight != height or m_SwapchainWidth != width )
  {
    m_SwapchainWidth  = std::max( 1u, width );
    m_SwapchainHeight = std::max( 1u, height );

    WaitIdle();

    for ( int i = 0; i < kNumFrames; ++i )
    {
      m_Backbuffers[i].Reset();
      // Restart the count from here. Everyone should have waited for this.
      m_FenceValues[i] = m_FenceValues[m_CurrentBackbufferIndex];
    }

    DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
    ERR_ABORT( m_Swapchain->GetDesc( &swapchain_desc ) );

    ERR_ABORT( m_Swapchain->ResizeBuffers(
        kNumFrames, m_SwapchainWidth, m_SwapchainHeight, swapchain_desc.BufferDesc.Format, swapchain_desc.Flags ) );

    m_CurrentBackbufferIndex = m_Swapchain->GetCurrentBackBufferIndex();

    UpdateRenderTargetViews( m_Device, m_Swapchain, m_RTVDescriptorHeap, m_RTVDescriptorSize, m_Backbuffers );
  }
}

Ember::Buffer Ember::RenderDevice::CreateUniformBuffer( size_t const size )
{
  return m_BufferManager.CreateUniformBuffer( m_Allocator.Get(), size );
}

void Ember::RenderDevice::WaitIdle()
{
  auto const wait_on_fence_value = ++m_CurrentFenceValue;

  // Signal on commandQueue.
  ERR_ABORT( m_DirectQueue->Signal( m_Fence.Get(), wait_on_fence_value ) );

  // Wait on CPU.
  // If the fence is less than 'value' we need to wait.
  if ( m_Fence->GetCompletedValue() < wait_on_fence_value )
  {
    // Set the event on fence.
    ERR_ABORT( m_Fence->SetEventOnCompletion( wait_on_fence_value, m_FenceEvent ) );

    // Wait for the event (with max timeout).
    ::WaitForSingleObject( m_FenceEvent, INFINITE );
  }
}

bool Ember::RenderDevice::IsInit() const
{
  return m_FenceEvent;
}

ID3D12CommandAllocator* Ember::RenderDevice::GetCurrentCommandAllocator() const
{
  return m_CommandAllocators[m_CurrentBackbufferIndex].Get();
}

ID3D12Resource* Ember::RenderDevice::GetCurrentBackbuffer() const
{
  return m_Backbuffers[m_CurrentBackbufferIndex].Get();
}

ID3D12GraphicsCommandList* Ember::RenderDevice::GetGraphicsCommandList() const
{
  return m_CommandList.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Ember::RenderDevice::GetCurrentRTVCpuDescriptorHandle() const
{
  return CD3DX12_CPU_DESCRIPTOR_HANDLE(
      m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_CurrentBackbufferIndex, m_RTVDescriptorSize );
}

void Ember::RenderDevice::ExecuteCommandList( ID3D12CommandList* command_list ) const
{
  m_DirectQueue->ExecuteCommandLists( 1, &command_list );
}

void Ember::RenderDevice::Present()
{
  bool const is_vsync_enabled = IsVsyncEnabled();
  bool const allow_tearing    = IsTearingSupported() and not is_vsync_enabled;
  UINT const sync_interval    = is_vsync_enabled ? 1 : 0;
  UINT const present_flags    = allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;

  ERR_ABORT( m_Swapchain->Present( sync_interval, present_flags ) );

  uint64_t const next_fence_value = ++m_CurrentFenceValue;
  ERR_ABORT( m_DirectQueue->Signal( m_Fence.Get(), next_fence_value ) );
  m_FenceValues[m_CurrentBackbufferIndex] = next_fence_value;

  // At the end of the queue, we wait for the next frame.
  m_CurrentBackbufferIndex  = m_Swapchain->GetCurrentBackBufferIndex();

  uint64_t const wait_value = m_FenceValues[m_CurrentBackbufferIndex];
  if ( m_Fence->GetCompletedValue() < wait_value )
  {
    // Set the event on fence.
    ERR_ABORT( m_Fence->SetEventOnCompletion( wait_value, m_FenceEvent ) );

    // Wait for the event (with max timeout).
    ::WaitForSingleObject( m_FenceEvent, INFINITE );
  }
}

bool Ember::RenderDevice::IsVsyncEnabled() const
{
  return m_VsyncAndTearing & kUseVSyncBit;
}

bool Ember::RenderDevice::IsTearingSupported() const
{
  return m_VsyncAndTearing & kSupportTearingBit;
}

void UpdateRenderTargetViews(
    ComPtr<ID3D12Device> const&              device,
    ComPtr<IDXGISwapChain4> const&           swapchain,
    ComPtr<ID3D12DescriptorHeap> const&      rtv_descriptor_heap,
    UINT const                               rtv_descriptor_size,
    std::span<ComPtr<ID3D12Resource>> const& backbuffers )
{
  size_t const                  backbuffer_count = backbuffers.size();

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle{ rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart() };
  // Fetch all backbuffers, create RTV and write them to the heap.
  for ( int i = 0; i < backbuffer_count; ++i )
  {
    // Write the swapchain backbuffer into the array.
    ERR_ABORT( swapchain->GetBuffer( i, IID_PPV_ARGS( &backbuffers[i] ) ) );

    // Creates the view for the backbuffer and places it at the rtvHandle.
    device->CreateRenderTargetView( backbuffers[i].Get(), nullptr, rtv_handle );

    // Increment rtvHandle by the size of rtvDescriptor.
    rtv_handle.Offset( ( INT )rtv_descriptor_size );
  }
}
