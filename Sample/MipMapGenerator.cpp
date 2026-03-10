#include "MipMapGenerator.hpp"

#include <Graphics/RenderDevice.hpp>
#include <Graphics/Texture.hpp>
#include <Util/DataUtil.hpp>
#include <Util/HelperUtils.hpp>

Ember::MipMapGenerator::MipMapGenerator(
    RenderDevice*               render_device,
    ComPtr<ID3D12RootSignature> root_signature,
    ComPtr<ID3D12PipelineState> pipeline,
    ComPtr<ID3D12PipelineState> cube_pipeline )
  : m_RenderDevice{ render_device }
  , m_RootSignature{ std::move( root_signature ) }
  , m_Pipeline{ std::move( pipeline ) }
  , m_CubePipeline{ std::move( cube_pipeline ) }
{}

bool Ember::MipMapGenerator::Create( MipMapGenerator* generator, RenderDevice* render_device )
{
  D3D12_STATIC_SAMPLER_DESC static_samplers[] = {
    CD3DX12_STATIC_SAMPLER_DESC{
                                0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, },
  };

  D3D12_ROOT_PARAMETER1 root_parameters[] = {
    RootConstants{ .Register = 0, .SizeBytes = sizeof( RootSigInfo ) },
  };

  auto root_signature       = render_device->CreateRootSignature( {
            .RootParameters = root_parameters,
            .StaticSamplers = static_samplers,
            .ShaderAccess   = RootSignatureDesc::Access::kCompute,
            .DebugName      = "Mip Map RootSig",
  } );

  auto mipmap_pipeline      = render_device->CreateComputePipeline( {
           .RootSignature     = root_signature.Get(),
           .ComputeShaderName = "MipMap.cso",
           .DebugName         = "Mip Map Pipeline",
  } );

  auto mipmap_cube_pipeline = render_device->CreateComputePipeline( {
      .RootSignature     = root_signature.Get(),
      .ComputeShaderName = "MipMapCube.cso",
      .DebugName         = "Mip Map Cube Pipeline",
  } );

  new ( generator ) MipMapGenerator{
    render_device,
    std::move( root_signature ),
    std::move( mipmap_pipeline ),
    std::move( mipmap_cube_pipeline ),
  };

  return true;
}

// Thread unsafe
bool Ember::MipMapGenerator::TryGenerateMipMaps( CommandList* command_list, Texture* texture ) const
{
  ComPtr<ID3D12Resource> uav_capable;

  // Grab the 'necessary' info from texture, and copy it to the 'alias' resource.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::Allocation* allocation = texture->GetAllocation();
  ID3D12Resource*      resource   = allocation->GetResource();
#else
  ID3D12Resource* resource = texture->GetTexture();
#endif

  D3D12_RESOURCE_DESC desc      = resource->GetDesc();
  DXGI_FORMAT         in_format = desc.Format;

  desc.Flags  &= ~( D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
  desc.Flags  |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Format  = DirectX::MakeLinear( desc.Format );

#if not defined( RENDERDOC_COMPAT )
  bool const is_aliased = allocation->GetHeap() != nullptr;
  if ( is_aliased )
  {
    // Placed resource can be aliased.
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateAliasingResource(
            allocation, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &uav_capable ) ),
        false );
  }
  else
  {
    D3D12MA::ALLOCATION_DESC allocation_desc{
      .Flags    = D3D12MA::ALLOCATION_FLAG_CAN_ALIAS,
      .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };
    ComPtr<D3D12MA::Allocation> copy_alloc;
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateResource(
            &allocation_desc,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            &copy_alloc,
            IID_PPV_ARGS( &uav_capable ) ),
        false );
    command_list->Track( std::move( copy_alloc ) );
  }
#else
  CD3DX12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_FAIL_RET_V(
      m_RenderDevice->GetDevice()->CreateCommittedResource(
          &properties,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS( &uav_capable ) ),
      false );
#endif

  ERR_FAIL_RET_V( uav_capable->SetName( L"UAV Alias" ), false );
  {
    CD3DX12_RESOURCE_BARRIER const transition = CD3DX12_RESOURCE_BARRIER::Transition(
        resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE );
    command_list->ResourceBarrier( transition );
  }

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER aliasing_barrier =
        CD3DX12_RESOURCE_BARRIER::Aliasing( allocation->GetResource(), uav_capable.Get() );
    command_list->ResourceBarrier( aliasing_barrier );
  }
#endif

  command_list->CopyResource( uav_capable.Get(), resource );

  SRVHandle mip_src_handle =
      command_list->Bind( BindTransient( uav_capable, CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D( in_format ) ) );
  UAVHandle mip_dst_handles[kMaxMipCount];

  for ( int level = 0; level < desc.MipLevels; level++ )
  {
    mip_dst_handles[level] = command_list->Bind(
        BindTransient( uav_capable, CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D( desc.Format, level ) ) );
  }

  uint32_t constexpr static kThreadGroupX      = 8;
  uint32_t constexpr static kThreadGroupY      = 8;
  uint32_t constexpr static kThreadGroupZ      = 1;

  uint32_t                 tex_width           = ( UINT )desc.Width;
  uint32_t                 tex_height          = desc.Height;

  CD3DX12_RESOURCE_BARRIER pre_compute_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      uav_capable.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

  command_list->ResourceBarrier( pre_compute_barrier );

  command_list->SetPipelineState( m_Pipeline.Get() );
  command_list->SetComputeRootSignature( m_RootSignature.Get() );

  RootSigInfo mip_map_info;
  mip_map_info.Src                           = mip_src_handle;
  mip_map_info.IsSrgb                        = DirectX::IsSRGB( in_format );

  CD3DX12_RESOURCE_BARRIER inter_mip_barrier = CD3DX12_RESOURCE_BARRIER::UAV( uav_capable.Get() );
  for ( int write_lvl = 1; write_lvl < desc.MipLevels; write_lvl++ )
  {
    tex_width                = std::max( tex_width / 2, 1u );
    tex_height               = std::max( tex_height / 2, 1u );

    mip_map_info.TexelSize   = { 1.0f / ( float )tex_width, 1.0f / ( float )tex_height };
    mip_map_info.Dst         = mip_dst_handles[write_lvl];
    mip_map_info.SrcMipLevel = write_lvl - 1;

    command_list->SetComputeRootConstants( 0, mip_map_info );
    command_list->Dispatch( {
        .X = std::max( 1u, tex_width / kThreadGroupX ),
        .Y = std::max( 1u, tex_height / kThreadGroupY ),
        .Z = std::max( 1u, 1 / kThreadGroupZ ),
    } );
    command_list->ResourceBarrier( inter_mip_barrier );
  }

  {
    D3D12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(
          uav_capable.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE ),
      CD3DX12_RESOURCE_BARRIER::Transition(
          resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST ),
    };
    command_list->ResourceBarrier( barriers );
  }
  command_list->CopyResource( resource, uav_capable.Get() );

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    command_list->ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Aliasing( uav_capable.Get(), resource ) );
  }
#endif

  return true;
}

bool Ember::MipMapGenerator::TryGenerateMipMapCube( CommandList* command_list, Texture* texture ) const
{
  ComPtr<ID3D12Resource> uav_capable;
  auto const             texture_resource_state = texture->GetCurrentState();

  // Grab the 'necessary' info from texture, and copy it to the 'alias' resource.
#if not defined( RENDERDOC_COMPAT )
  D3D12MA::Allocation* allocation = texture->GetAllocation();
  ID3D12Resource*      resource   = allocation->GetResource();
#else
  ID3D12Resource* resource = texture->GetTexture();
#endif

  D3D12_RESOURCE_DESC desc      = resource->GetDesc();
  DXGI_FORMAT const   in_format = desc.Format;

  desc.Flags  &= ~( D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
  desc.Flags  |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Format  = DirectX::MakeLinear( desc.Format );

#if not defined( RENDERDOC_COMPAT )
  bool const is_aliased = allocation->GetHeap() != nullptr;
  if ( is_aliased )
  {
    // Placed resource can be aliased.
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateAliasingResource(
            allocation, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &uav_capable ) ),
        false );
  }
  else
  {
    D3D12MA::ALLOCATION_DESC allocation_desc{
      .Flags    = D3D12MA::ALLOCATION_FLAG_CAN_ALIAS,
      .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };
    ComPtr<D3D12MA::Allocation> copy_alloc;
    ERR_FAIL_RET_V(
        m_RenderDevice->GetAllocator()->CreateResource(
            &allocation_desc,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            &copy_alloc,
            IID_PPV_ARGS( &uav_capable ) ),
        false );
    command_list->Track( std::move( copy_alloc ) );
  }
#else
  CD3DX12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
  ERR_FAIL_RET_V(
      m_RenderDevice->GetDevice()->CreateCommittedResource(
          &properties,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS( &uav_capable ) ),
      false );
#endif

  ERR_FAIL_RET_V( uav_capable->SetName( L"UAV Alias" ), false );
  if ( texture_resource_state != D3D12_RESOURCE_STATE_COPY_SOURCE )
  {
    CD3DX12_RESOURCE_BARRIER const transition =
        CD3DX12_RESOURCE_BARRIER::Transition( resource, texture_resource_state, D3D12_RESOURCE_STATE_COPY_SOURCE );
    command_list->ResourceBarrier( transition );
  }

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER aliasing_barrier =
        CD3DX12_RESOURCE_BARRIER::Aliasing( allocation->GetResource(), uav_capable.Get() );
    command_list->ResourceBarrier( aliasing_barrier );
  }
#endif

  command_list->CopyResource( uav_capable.Get(), resource );

  SRVHandle mip_src_handle =
      command_list->Bind( BindTransient( uav_capable, CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube( in_format ) ) );
  UAVHandle mip_dst_handles[kMaxMipCount];

  for ( uint32_t level = 0; level < desc.MipLevels; level++ )
  {
    mip_dst_handles[level] = command_list->Bind(
        BindTransient( uav_capable, CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray( desc.Format, 6, 0, level ) ) );
  }

  uint32_t constexpr static kThreadGroupX = 8;
  uint32_t constexpr static kThreadGroupY = 8;
  uint32_t constexpr static kThreadGroupZ = 1;

  uint32_t tex_width                      = ( UINT )desc.Width;
  uint32_t tex_height                     = desc.Height;

  auto     pre_compute_barrier            = CD3DX12_RESOURCE_BARRIER::Transition(
      uav_capable.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

  command_list->ResourceBarrier( pre_compute_barrier );

  command_list->SetPipelineState( m_CubePipeline.Get() );
  command_list->SetComputeRootSignature( m_RootSignature.Get() );

  RootSigInfo mip_map_info;
  mip_map_info.Src       = mip_src_handle;
  mip_map_info.IsSrgb    = DirectX::IsSRGB( in_format );

  auto inter_mip_barrier = CD3DX12_RESOURCE_BARRIER::UAV( uav_capable.Get() );
  for ( uint32_t write_lvl = 1; write_lvl < desc.MipLevels; write_lvl++ )
  {
    tex_width                = std::max( tex_width / 2, 1u );
    tex_height               = std::max( tex_height / 2, 1u );

    mip_map_info.TexelSize   = { 1.0f / ( float )tex_width, 1.0f / ( float )tex_height };
    mip_map_info.Dst         = mip_dst_handles[write_lvl];
    mip_map_info.SrcMipLevel = write_lvl - 1;

    command_list->SetComputeRootConstants( 0, mip_map_info );
    command_list->Dispatch( {
        .X = std::max( 1u, tex_width / kThreadGroupX ),
        .Y = std::max( 1u, tex_height / kThreadGroupY ),
        .Z = std::max( 1u, 6 / kThreadGroupZ ),
    } );
    command_list->ResourceBarrier( inter_mip_barrier );
  }

  {
    D3D12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(
          uav_capable.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE ),
      CD3DX12_RESOURCE_BARRIER::Transition(
          resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST ),
    };
    command_list->ResourceBarrier( barriers );
  }
  command_list->CopyResource( resource, uav_capable.Get() );

#if not defined( RENDERDOC_COMPAT )
  if ( is_aliased )
  {
    CD3DX12_RESOURCE_BARRIER reverse_aliasing = CD3DX12_RESOURCE_BARRIER::Aliasing( uav_capable.Get(), resource );
    command_list->ResourceBarrier( reverse_aliasing );
  }
#endif

  if ( texture_resource_state != D3D12_RESOURCE_STATE_COPY_DEST )
  {
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition( resource, D3D12_RESOURCE_STATE_COPY_DEST, texture_resource_state );
    command_list->ResourceBarrier( barrier );
  }

  return true;
}
