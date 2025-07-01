#include "RenderDevice.hpp"

#include "Base/DirectXHeaders.hpp"
#include "Base/HelperUtils.hpp"
#include "Base/Runtime.hpp"

#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "D3DCompiler.lib" )
#pragma comment( lib, "dxgi.lib" )

Ember::RenderDevice::RenderDevice(
    ComPtr<ID3D12Device> const&                   device,
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
    HANDLE const                                  fence_event )
  : m_Device{ device }
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
  , m_IsInitialized{ true }
{}

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
      .BufferCount = NUM_FRAMES,
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
  std::vector<ComPtr<ID3D12Resource>> backbuffers( NUM_FRAMES );
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = NUM_FRAMES,
    };

    ERR_ABORT( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &rtv_descriptor_heap ) ) );
    rtv_descriptor_size = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle{ rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart() };

    // Fetch all backbuffers, create RTV and write them to the heap.
    for ( int i = 0; i < NUM_FRAMES; ++i )
    {
      // Write the swapchain backbuffer into the array.
      ERR_ABORT( swapchain->GetBuffer( i, IID_PPV_ARGS( &backbuffers[i] ) ) );

      // Creates the view for the backbuffer and places it at the rtvHandle.
      device->CreateRenderTargetView( backbuffers[i].Get(), nullptr, rtv_handle );

      // Increment rtvHandle by the size of rtvDescriptor.
      rtv_handle.Offset( rtv_descriptor_size );
    }
  }

  // Create Command Allocator
  std::vector<ComPtr<ID3D12CommandAllocator>> command_allocators( NUM_FRAMES );
  for ( int i = 0; i < NUM_FRAMES; ++i )
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

  return RenderDevice{
    device,
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
  };
}

void Ember::RenderDevice::Destroy()
{
  if ( m_IsInitialized )
  {
    ::CloseHandle( m_FenceEvent );
    m_IsInitialized = false;
  }
}

Ember::RenderDevice::~RenderDevice()
{
  ASSERT( not m_IsInitialized );
}
