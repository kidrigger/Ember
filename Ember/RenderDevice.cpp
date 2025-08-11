#include "RenderDevice.hpp"

#include <span>

#include "BindlessManager.hpp"
#include "Buffer.hpp"
#include "Util/DataUtil.hpp"
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
    ComPtr<ID3D12Device2>               device,
    ComPtr<D3D12MA::Allocator>          allocator,
    uint32_t const                      swapchain_width,
    uint32_t const                      swapchain_height,
    ComPtr<IDXGISwapChain4>             swapchain,
    std::vector<ComPtr<ID3D12Resource>> backbuffers,
    ComPtr<ID3D12DescriptorHeap>        rtv_descriptor_heap,
    uint32_t const                      rtv_descriptor_size,
    ComPtr<ID3D12DescriptorHeap>        dsv_descriptor_heap,
    std::unique_ptr<BindlessManager>    bindless_manager,
    Context                             direct_context,
    bool const                          is_tearing_supported )
  : m_Device{ std::move( device ) }
  , m_Allocator{ std::move( allocator ) }
  , m_SwapchainWidth{ swapchain_width }
  , m_SwapchainHeight{ swapchain_height }
  , m_Swapchain{ std::move( swapchain ) }
  , m_Backbuffers{ std::move( backbuffers ) }
  , m_RTVDescriptorHeap{ std::move( rtv_descriptor_heap ) }
  , m_RTVDescriptorSize{ rtv_descriptor_size }
  , m_DSVDescriptorHeap{ std::move( dsv_descriptor_heap ) }
  , m_Bindless{ std::move( bindless_manager ) }
  , m_BufferManager{ m_Device, m_Allocator, m_Bindless.get() }
  , m_TextureManager{ m_Device, m_Allocator, m_Bindless.get() }
  , m_DirectContext{ std::move( direct_context ) }
{
  auto const always_true_receipt = m_DirectContext.CreateReceipt();
  m_FrameReceipts.resize( m_Backbuffers.size(), always_true_receipt );

  if ( is_tearing_supported )
  {
    m_VsyncAndTearing = m_VsyncAndTearing | kSupportTearingBit;
  }
}

ComPtr<ID3D12Device2> Ember::RenderDevice::GetDevice() noexcept
{
  return m_Device;
}

ComPtr<D3D12MA::Allocator> Ember::RenderDevice::GetAllocator() noexcept
{
  return m_Allocator;
}

DXGI_FORMAT Ember::RenderDevice::FetchSwapchainFormat() const
{
  DXGI_SWAP_CHAIN_DESC desc;
  ERR_ABORT( m_Swapchain->GetDesc( &desc ) );
  return desc.BufferDesc.Format;
}

D3D_ROOT_SIGNATURE_VERSION Ember::RenderDevice::FetchHighestRootSignatureVersion() const
{
  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  if ( FAILED( m_Device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof( feature_data ) ) ) )
  {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }
  return feature_data.HighestVersion;
}

void Ember::RenderDevice::Create( RenderDevice* render_device, HWND window_handle, bool const use_warp )
{
#if defined( _DEBUG ) or defined( RELDEBUG )
  {
    ComPtr<ID3D12Debug>  debug_interface;
    ComPtr<ID3D12Debug1> debug_interface1;
    ERR_ABORT( D3D12GetDebugInterface( IID_PPV_ARGS( &debug_interface ) ) );
    ERR_ABORT( debug_interface.As( &debug_interface1 ) );
    debug_interface->EnableDebugLayer();
    debug_interface1->SetEnableGPUBasedValidation( true );
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
          .NumSeverities = CountOf( severities ),
          .pSeverityList = DataOf( severities ),
          .NumIDs = CountOf( deny_ids ),
          .pIDList = DataOf( deny_ids ),
        },
      };

      ERR_ABORT( info_queue->PushStorageFilter( &new_filter ) );
    }
#endif
  }

  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS feature_data;
    ZeroMemory( &feature_data, sizeof( feature_data ) );
    if ( SUCCEEDED(
             device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &feature_data, sizeof( feature_data ) ) ) )
    {
      if ( feature_data.TypedUAVLoadAdditionalFormats )
      {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = { DXGI_FORMAT_R11G11B10_FLOAT,
                                                             D3D12_FORMAT_SUPPORT1_NONE,
                                                             D3D12_FORMAT_SUPPORT2_NONE };
        if ( SUCCEEDED( device->CheckFeatureSupport(
                 D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof( format_support ) ) ) )
        {

          ASSERT(
              format_support.Support2 &
              ( D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE ) );
        }
      }
    }
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

  // Context Creation
  Context direct_context;
  Context::Create( &direct_context, device, D3D12_COMMAND_LIST_TYPE_DIRECT );

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
      .Flags       = is_tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : ( UINT )0,
    };

    ComPtr<IDXGISwapChain1> swapchain1;
    ERR_ABORT( dxgi_factory->CreateSwapChainForHwnd(
        direct_context.GetCommandQueue(), window_handle, &swapchain_desc, nullptr, nullptr, &swapchain1 ) );

    ERR_ABORT( swapchain1.As( &swapchain ) );
  }

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

  ComPtr<ID3D12DescriptorHeap> dsv_descriptor_heap;
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
      .NumDescriptors = 1,
    };

    ERR_ABORT( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &dsv_descriptor_heap ) ) );
  }

  auto bindless_manager = std::make_unique_for_overwrite<BindlessManager>();
  BindlessManager::Create( bindless_manager.get(), device, 10'000, 1000 );

  new ( render_device ) RenderDevice{
    std::move( device ),
    std::move( allocator ),
    width,
    height,
    std::move( swapchain ),
    std::move( backbuffers ),
    std::move( rtv_descriptor_heap ),
    rtv_descriptor_size,
    std::move( dsv_descriptor_heap ),
    std::move( bindless_manager ),
    std::move( direct_context ),
    is_tearing_supported,
  };
}

void Ember::RenderDevice::ResizeSwapchain( uint32_t const width, uint32_t const height )
{
  if ( m_SwapchainHeight != height or m_SwapchainWidth != width )
  {
    m_SwapchainWidth  = std::max( 1u, width );
    m_SwapchainHeight = std::max( 1u, height );

    m_DirectContext.WaitIdle();

    for ( int i = 0; i < kNumFrames; ++i )
    {
      m_Backbuffers[i].Reset();
    }

    DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
    ERR_ABORT( m_Swapchain->GetDesc( &swapchain_desc ) );

    ERR_ABORT( m_Swapchain->ResizeBuffers(
        kNumFrames, m_SwapchainWidth, m_SwapchainHeight, swapchain_desc.BufferDesc.Format, swapchain_desc.Flags ) );

    m_CurrentBackbufferIndex = m_Swapchain->GetCurrentBackBufferIndex();

    UpdateRenderTargetViews( m_Device, m_Swapchain, m_RTVDescriptorHeap, m_RTVDescriptorSize, m_Backbuffers );
  }
}

Ember::Buffer Ember::RenderDevice::CreateVertexBuffer( uint32_t const size, uint32_t const stride )
{
  return m_BufferManager.CreateVertexBuffer( size, stride );
}

Ember::Buffer Ember::RenderDevice::CreateIndexBuffer( uint32_t const size, DXGI_FORMAT const format )
{
  return m_BufferManager.CreateIndexBuffer( size, format );
}

Ember::Buffer Ember::RenderDevice::CreateStorageBuffer( uint32_t const size, uint32_t const stride )
{
  return m_BufferManager.CreateStorageBuffer( size, stride );
}

Ember::Buffer Ember::RenderDevice::CreateConstantBuffer( uint32_t const size )
{
  return m_BufferManager.CreateConstantBuffer( size );
}

Ember::Texture Ember::RenderDevice::CreateTexture2D( Texture2DCreateInfo const& create_info )
{
  return m_TextureManager.CreateTexture2D( create_info );
}

Ember::Texture Ember::RenderDevice::CreateTextureCube( TextureCubeCreateInfo const& create_info )
{
  return m_TextureManager.CreateTextureCube( create_info );
}

Ember::Sampler Ember::RenderDevice::CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc )
{
  return m_TextureManager.CreateSampler( sampler_desc );
}

Ember::SRVHandle Ember::RenderDevice::CreateBindlessHandle(
    ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc ) const noexcept
{
  return m_Bindless->CreateDescriptorHandle( resource, srv_desc );
}

Ember::UAVHandle Ember::RenderDevice::CreateBindlessHandle(
    ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc ) const noexcept
{
  return m_Bindless->CreateDescriptorHandle( resource, uav_desc );
}

Ember::SamplerHandle Ember::RenderDevice::CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc ) const noexcept
{
  return m_Bindless->CreateSamplerHandle( sampler_desc );
}

std::array<ID3D12DescriptorHeap*, 2> Ember::RenderDevice::GetBindlessDescriptorHeaps() const
{
  return m_Bindless->GetBindlessDescriptorHeaps();
}

void Ember::RenderDevice::FreeHandle( CBVHandle const handle ) const
{
  m_Bindless->Free( handle );
}

void Ember::RenderDevice::FreeHandle( SRVHandle const handle ) const
{
  return m_Bindless->Free( handle );
}

void Ember::RenderDevice::FreeHandle( UAVHandle const handle ) const
{
  m_Bindless->Free( handle );
}

void Ember::RenderDevice::FreeHandle( SamplerHandle const handle ) const
{
  m_Bindless->Free( handle );
}

void Ember::RenderDevice::WaitOn( Context::Receipt const receipt ) const
{
  m_DirectContext.WaitOn( receipt );
}

void Ember::RenderDevice::QueueWaitOn( Context::Receipt const receipt ) const
{
  m_DirectContext.QueueWaitOn( receipt );
}

void Ember::RenderDevice::WaitIdle()
{
  m_DirectContext.WaitIdle();
}

ID3D12Resource* Ember::RenderDevice::GetCurrentBackbuffer() const noexcept
{
  return m_Backbuffers[m_CurrentBackbufferIndex].Get();
}

Ember::Context::CommandList Ember::RenderDevice::GetGraphicsCommandList() noexcept
{
  return m_DirectContext.GetCommandList();
}

uint32_t Ember::RenderDevice::GetCurrentFrameIndex() const noexcept
{
  return m_CurrentBackbufferIndex;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Ember::RenderDevice::GetCurrentRTVCpuDescriptorHandle() const noexcept
{
  return CD3DX12_CPU_DESCRIPTOR_HANDLE(
      m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_CurrentBackbufferIndex, m_RTVDescriptorSize );
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Ember::RenderDevice::GetCurrentDSVCpuDescriptorHandle() const noexcept
{
  return CD3DX12_CPU_DESCRIPTOR_HANDLE( m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );
}

void Ember::RenderDevice::ExecuteCommandList( Context::CommandList&& command_list )
{
  m_DirectContext.Submit( std::move( command_list ) );
}

void Ember::RenderDevice::Present()
{
  bool const is_vsync_enabled = IsVsyncEnabled();
  bool const allow_tearing    = IsTearingSupported() and not is_vsync_enabled;
  UINT const sync_interval    = is_vsync_enabled ? 1 : 0;
  UINT const present_flags    = allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;

  ERR_ABORT( m_Swapchain->Present( sync_interval, present_flags ) );

  m_FrameReceipts[m_CurrentBackbufferIndex] = m_DirectContext.Signal();

  // At the end of the queue, we wait for the next frame.
  m_CurrentBackbufferIndex = m_Swapchain->GetCurrentBackBufferIndex();

  m_DirectContext.WaitOn( m_FrameReceipts[m_CurrentBackbufferIndex] );
}

bool Ember::RenderDevice::IsVsyncEnabled() const
{
  return m_VsyncAndTearing & kUseVSyncBit;
}

bool Ember::RenderDevice::IsTearingSupported() const
{
  return m_VsyncAndTearing & kSupportTearingBit;
}

Ember::RenderDevice::~RenderDevice()
{
  if ( m_Device ) WaitIdle();
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
