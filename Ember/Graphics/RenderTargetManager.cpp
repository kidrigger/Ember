#include "RenderTargetManager.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Util/HelperUtils.hpp>

void Ember::RenderTargetManager::Create( RenderTargetManager* render_target_manager, ComPtr<ID3D12Device> d3d_device )
{
  D3D12_DESCRIPTOR_HEAP_DESC const rtv_desc{
    .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
    .NumDescriptors = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT,
    .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
  };
  D3D12_DESCRIPTOR_HEAP_DESC const dsv_desc{
    .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
    .NumDescriptors = 1,
    .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
  };

  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;

  ERR_ABORT( d3d_device->CreateDescriptorHeap( &rtv_desc, IID_PPV_ARGS( &rtv_heap ) ) );
  ERR_ABORT( d3d_device->CreateDescriptorHeap( &dsv_desc, IID_PPV_ARGS( &dsv_heap ) ) );

  uint32_t const rtv_descriptor_size = d3d_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
  uint32_t const dsv_descriptor_size = d3d_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );

  new ( render_target_manager ) RenderTargetManager{
    std::move( d3d_device ), std::move( rtv_heap ), rtv_descriptor_size, std::move( dsv_heap ), dsv_descriptor_size,
  };
}

Ember::RenderTargetManager::RenderTargetManager(
    ComPtr<ID3D12Device>         d3d12_device,
    ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap,
    uint32_t const               rtv_descriptor_size,
    ComPtr<ID3D12DescriptorHeap> dsv_descriptor_heap,
    uint32_t const               dsv_descriptor_size )
  : m_D3D12Device{ std::move( d3d12_device ) }
  , m_RTVDescriptorHeap{ std::move( rtv_descriptor_heap ) }
  , m_DSVDescriptorHeap{ std::move( dsv_descriptor_heap ) }
  , m_RTVDescriptorSize{ rtv_descriptor_size }
  , m_DSVDescriptorSize{ dsv_descriptor_size }
{}

void Ember::RenderTargetManager::RSSetScissorViewport(
    ID3D12GraphicsCommandList* command_list, uint32_t const width, uint32_t const height ) const
{
  D3D12_VIEWPORT const viewport{
    .TopLeftX = 0.0f,
    .TopLeftY = 0.0f,
    .Width    = ( FLOAT )width,
    .Height   = ( FLOAT )height,
    .MinDepth = 0.0f,
    .MaxDepth = 1.0f,
  };

  D3D12_RECT const scissor{
    .left   = 0,
    .top    = 0,
    .right  = ( LONG )width,
    .bottom = ( LONG )height,
  };

  command_list->RSSetViewports( 1, &viewport );
  command_list->RSSetScissorRects( 1, &scissor );
}

void Ember::RenderTargetManager::ClearRenderTargetView(
    ID3D12GraphicsCommandList* command_list, ID3D12Resource* render_target, float const color[] ) const
{
  // CreateRenderTargetView by default uses all the layers of the array
  // but only the top mip.
  auto const                        desc       = render_target->GetDesc();
  D3D12_CPU_DESCRIPTOR_HANDLE const rtv_handle = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  if ( desc.MipLevels == 1 )
  {
    m_D3D12Device->CreateRenderTargetView( render_target, nullptr, rtv_handle );
    command_list->ClearRenderTargetView( rtv_handle, color, 0, nullptr );

    return;
  }

  // So we need to manually clear the mips.
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
    .Format         = desc.Format,
    .ViewDimension  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
    .Texture2DArray = {
      .MipSlice        = 0,
      .FirstArraySlice = 0,
      .ArraySize       = desc.DepthOrArraySize,
      .PlaneSlice      = 0,
    },
  };
  for ( uint_fast16_t i = 0; i < desc.MipLevels; i++ )
  {
    rtv_desc.Texture2DArray.MipSlice = i;
    m_D3D12Device->CreateRenderTargetView( render_target, &rtv_desc, rtv_handle );

    command_list->ClearRenderTargetView( rtv_handle, color, 0, nullptr );
  }
}

void Ember::RenderTargetManager::ClearRenderTargetView(
    ID3D12GraphicsCommandList* command_list, Texture const& render_target, float const color[] ) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE const rtv_handle = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  m_D3D12Device->CreateRenderTargetView( render_target.GetTexture(), nullptr, rtv_handle );

  command_list->ClearRenderTargetView( rtv_handle, color, 0, nullptr );
}

void Ember::RenderTargetManager::ClearRenderTargetViews(
    ID3D12GraphicsCommandList* command_list,
    uint32_t const             count,
    ID3D12Resource**           render_targets,
    float const                color[] ) const
{
  auto const rtv_start  = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto       rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtv_start, 0, m_RTVDescriptorSize };

  for ( uint32_t i = 0; i < count; i++ )
  {
    m_D3D12Device->CreateRenderTargetView( render_targets[i], nullptr, rtv_handle );
    command_list->ClearRenderTargetView( rtv_handle, color, 0, nullptr );

    rtv_handle.Offset( ( INT )m_RTVDescriptorSize );
  }
}

void Ember::RenderTargetManager::ClearRenderTargetViews(
    ID3D12GraphicsCommandList* command_list,
    uint32_t const             count,
    Texture const*             render_targets,
    float const                color[] ) const
{
  auto const rtv_start  = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto       rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtv_start, 0, m_RTVDescriptorSize };

  for ( uint32_t i = 0; i < count; i++ )
  {
    m_D3D12Device->CreateRenderTargetView( render_targets[i].GetTexture(), nullptr, rtv_handle );
    command_list->ClearRenderTargetView( rtv_handle, color, 0, nullptr );

    rtv_handle.Offset( ( INT )m_RTVDescriptorSize );
  }
}

void Ember::RenderTargetManager::ClearDepthStencilView(
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource*            depth_stencil,
    D3D12_CLEAR_FLAGS const    flags,
    float const                depth,
    uint8_t const              stencil ) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE const dsv_descriptor = m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  m_D3D12Device->CreateDepthStencilView( depth_stencil, nullptr, dsv_descriptor );

  command_list->ClearDepthStencilView( dsv_descriptor, flags, depth, stencil, 0, nullptr );
}

void Ember::RenderTargetManager::ClearDepthStencilView(
    ID3D12GraphicsCommandList* command_list,
    Texture const&             depth_stencil,
    D3D12_CLEAR_FLAGS const    flags,
    float const                depth,
    uint8_t const              stencil ) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE const dsv_descriptor = m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  m_D3D12Device->CreateDepthStencilView( depth_stencil.GetTexture(), nullptr, dsv_descriptor );

  command_list->ClearDepthStencilView( dsv_descriptor, flags, depth, stencil, 0, nullptr );
}

void Ember::RenderTargetManager::OMSetRenderTargets(
    ID3D12GraphicsCommandList*           command_list,
    uint32_t const                       count,
    ID3D12Resource**                     render_targets,
    D3D12_RENDER_TARGET_VIEW_DESC const* rtv_desc,
    ID3D12Resource*                      depth_stencil,
    D3D12_DEPTH_STENCIL_VIEW_DESC const* dsv_desc ) const
{
  auto const rtv_start  = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto       rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtv_start, 0, m_RTVDescriptorSize };

  for ( uint32_t i = 0; i < count; i++ )
  {
    m_D3D12Device->CreateRenderTargetView( render_targets[i], rtv_desc ? &rtv_desc[i] : nullptr, rtv_handle );
    rtv_handle.Offset( ( INT )m_RTVDescriptorSize );
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const dsv_descriptor = m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  if ( depth_stencil )
  {
    m_D3D12Device->CreateDepthStencilView( depth_stencil, dsv_desc, dsv_descriptor );
  }

  command_list->OMSetRenderTargets( count, &rtv_start, TRUE, depth_stencil ? &dsv_descriptor : nullptr );
}

void Ember::RenderTargetManager::OMSetRenderTargets(
    ID3D12GraphicsCommandList* command_list,
    uint32_t const             count,
    Texture const*             render_targets,
    Texture const*             depth_stencil ) const
{
  auto const rtv_start  = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto       rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtv_start, 0, m_RTVDescriptorSize };

  for ( uint32_t i = 0; i < count; i++ )
  {
    m_D3D12Device->CreateRenderTargetView( render_targets[i].GetTexture(), nullptr, rtv_handle );
    rtv_handle.Offset( ( INT )m_RTVDescriptorSize );
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const dsv_descriptor = m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  if ( depth_stencil )
  {
    m_D3D12Device->CreateDepthStencilView( depth_stencil->GetTexture(), nullptr, dsv_descriptor );
  }

  command_list->OMSetRenderTargets( count, &rtv_start, TRUE, depth_stencil ? &dsv_descriptor : nullptr );
}

void Ember::RenderTargetManager::OMSetRenderTargets(
    ID3D12GraphicsCommandList*           command_list,
    uint32_t const                       count,
    Texture const*                       render_targets,
    D3D12_RENDER_TARGET_VIEW_DESC const* rtv_desc,
    Texture const*                       depth_stencil,
    D3D12_DEPTH_STENCIL_VIEW_DESC const* dsv_desc ) const
{
  auto const rtv_start  = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto       rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtv_start, 0, m_RTVDescriptorSize };

  for ( uint32_t i = 0; i < count; i++ )
  {
    m_D3D12Device->CreateRenderTargetView(
        render_targets[i].GetTexture(), rtv_desc ? rtv_desc + i : nullptr, rtv_handle );
    rtv_handle.Offset( ( INT )m_RTVDescriptorSize );
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const dsv_descriptor = m_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  if ( depth_stencil )
  {
    m_D3D12Device->CreateDepthStencilView( depth_stencil->GetTexture(), dsv_desc ? dsv_desc : nullptr, dsv_descriptor );
  }

  command_list->OMSetRenderTargets( count, &rtv_start, TRUE, depth_stencil ? &dsv_descriptor : nullptr );
}
