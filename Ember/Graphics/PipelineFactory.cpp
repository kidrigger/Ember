#include "PipelineFactory.hpp"

#include "Graphics/RenderDevice.hpp"
#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/StringUtil.hpp"

#include <sstream>

Ember::RootConstants::operator D3D12_ROOT_PARAMETER1() const noexcept
{
  ASSERT( ( SizeBytes % 4 ) == 0 );
  return {
      .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
      .Constants        = { .ShaderRegister = Register, .RegisterSpace = Space, .Num32BitValues = SizeBytes / 4, },
      .ShaderVisibility = Visibility
    };
}

Ember::RootConstantBuffer::operator D3D12_ROOT_PARAMETER1() const noexcept
{
  return {
      .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV,
      .Descriptor       = { .ShaderRegister = Register, .RegisterSpace = Space, .Flags = Flags, },
      .ShaderVisibility = Visibility,
    };
}

Ember::Pipeline::Pipeline( ComPtr<ID3D12RootSignature> root_signature, ComPtr<ID3D12PipelineState> pipeline )
  : m_RootSignature{ std::move( root_signature ) }, m_Pipeline{ std::move( pipeline ) }
{}

ID3D12RootSignature* Ember::Pipeline::GetRootSignature() const noexcept
{
  return m_RootSignature.Get();
}

ID3D12PipelineState* Ember::Pipeline::GetPipeline() const noexcept
{
  return m_Pipeline.Get();
}

Ember::PipelineFactory::PipelineFactory(
    ComPtr<ID3D12Device5> d3d_device, D3D_ROOT_SIGNATURE_VERSION const root_signature_version )
  : m_D3DDevice{ std::move( d3d_device ) }, m_RootSignatureVersion{ root_signature_version }
{}

ComPtr<ID3D12RootSignature> Ember::PipelineFactory::CreateRootSignature( RootSignatureDesc const& desc ) const
{
  D3D12_ROOT_SIGNATURE_FLAGS const root_signature_flags =
      ( D3D12_ROOT_SIGNATURE_FLAGS )desc.ShaderAccess | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(
      CountOf( desc.RootParameters ),
      DataOf( desc.RootParameters ),
      CountOf( desc.StaticSamplers ),
      DataOf( desc.StaticSamplers ),
      root_signature_flags );

  ComPtr<ID3DBlob> root_signature_blob;
  ComPtr<ID3DBlob> error_blob;
  ERR_FAIL_RET_V(
      D3DX12SerializeVersionedRootSignature(
          &root_signature_desc, m_RootSignatureVersion, &root_signature_blob, &error_blob ),
      nullptr );

  ComPtr<ID3D12RootSignature> root_signature;
  ERR_FAIL_RET_V(
      m_D3DDevice->CreateRootSignature(
          0,
          root_signature_blob->GetBufferPointer(),
          root_signature_blob->GetBufferSize(),
          IID_PPV_ARGS( &root_signature ) ),
      nullptr );

  wchar_t buf[256];
  ToWideChar( buf, desc.DebugName );
  ERR_FAIL_RET_V( root_signature->SetName( buf ), nullptr );

  return root_signature;
}

ComPtr<ID3D12PipelineState> Ember::PipelineFactory::CreateGraphicsPipeline( GraphicsPipelineDesc const& desc ) const
{
  std::vector<byte>             stream;
  std::vector<ComPtr<ID3DBlob>> blobs;

  auto                          push_to_stream = [&stream]( auto const& data )
  {
    auto const* ptr = reinterpret_cast<byte const*>( std::addressof( data ) );
    stream.insert( stream.end(), ptr, ptr + sizeof( data ) );
  };

  ASSERT( desc.RootSignature );
  CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature{ desc.RootSignature };
  push_to_stream( root_signature );

  CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitive_topology{ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE };
  push_to_stream( primitive_topology );

  wchar_t wide_string_buf[256];

  if ( not desc.VertexShaderName.empty() )
  {
    ToWideChar( wide_string_buf, desc.VertexShaderName );

    auto& vertex_shader_blob = blobs.emplace_back();
    ERR_FAIL_RET_V( D3DReadFileToBlob( wide_string_buf, &vertex_shader_blob ), {} );

    CD3DX12_PIPELINE_STATE_STREAM_VS vertex_shader{ CD3DX12_SHADER_BYTECODE{ vertex_shader_blob.Get() } };
    push_to_stream( vertex_shader );
  }

  if ( not desc.AmpShaderName.empty() )
  {
    ToWideChar( wide_string_buf, desc.AmpShaderName );

    auto& amp_shader_blob = blobs.emplace_back();
    ERR_FAIL_RET_V( D3DReadFileToBlob( wide_string_buf, &amp_shader_blob ), {} );

    CD3DX12_PIPELINE_STATE_STREAM_AS amp_shader{ CD3DX12_SHADER_BYTECODE{ amp_shader_blob.Get() } };
    push_to_stream( amp_shader );
  }

  if ( not desc.MeshShaderName.empty() )
  {
    ToWideChar( wide_string_buf, desc.MeshShaderName );

    auto& mesh_shader_blob = blobs.emplace_back();
    ERR_FAIL_RET_V( D3DReadFileToBlob( wide_string_buf, &mesh_shader_blob ), {} );

    CD3DX12_PIPELINE_STATE_STREAM_MS mesh_shader{ CD3DX12_SHADER_BYTECODE{ mesh_shader_blob.Get() } };
    push_to_stream( mesh_shader );
  }

  if ( not desc.PixelShaderName.empty() )
  {
    ToWideChar( wide_string_buf, desc.PixelShaderName );

    auto& pixel_shader_blob = blobs.emplace_back();
    ERR_FAIL_RET_V( D3DReadFileToBlob( wide_string_buf, &pixel_shader_blob ), {} );

    CD3DX12_PIPELINE_STATE_STREAM_PS pixel_shader{ CD3DX12_SHADER_BYTECODE{ pixel_shader_blob.Get() } };
    push_to_stream( pixel_shader );
  }

  {
    auto const               raster = desc.RasterizerDesc.value_or( {} );
    CD3DX12_RASTERIZER_DESC2 raster_desc{ D3D12_DEFAULT };
    switch ( raster.CullMode )
    {
      case Rasterizer::CullMode::kNone:
        raster_desc.CullMode = D3D12_CULL_MODE_NONE;
        break;
      case Rasterizer::CullMode::kFront:
        raster_desc.CullMode = D3D12_CULL_MODE_FRONT;
        break;
      case Rasterizer::CullMode::kBack:
        raster_desc.CullMode = D3D12_CULL_MODE_BACK;
        break;
    }
    switch ( raster.FrontFace )
    {
      case Rasterizer::FrontFace::kClockwise:
        raster_desc.FrontCounterClockwise = FALSE;
        break;
      case Rasterizer::FrontFace::kCounterClockwise:
        raster_desc.FrontCounterClockwise = TRUE;
        break;
    }
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER2 rasterizer{ raster_desc };
    push_to_stream( rasterizer );
  }

  if ( desc.BlendDesc.has_value() )
  {
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend{ desc.BlendDesc.value() };
    push_to_stream( blend );
  }

  if ( desc.DepthStencilDesc.has_value() )
  {
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depth_stencil{ desc.DepthStencilDesc.value() };
    push_to_stream( depth_stencil );
  }

  if ( not desc.RTVFormats.empty() )
  {
    D3D12_RT_FORMAT_ARRAY rt_formats{};
    rt_formats.NumRenderTargets = CountOf( desc.RTVFormats );
    std::ranges::copy( desc.RTVFormats, rt_formats.RTFormats );

    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtv_formats{ rt_formats };
    push_to_stream( rtv_formats );
  }

  if ( desc.DSVFormat.has_value() )
  {
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsv_format{ desc.DSVFormat.value() };
    push_to_stream( dsv_format );
  }

  D3D12_PIPELINE_STATE_STREAM_DESC const pipeline_state_stream_desc = {
    .SizeInBytes                   = ByteSizeOf( stream ),
    .pPipelineStateSubobjectStream = DataOf( stream ),
  };

  ComPtr<ID3D12PipelineState> pipeline;
  ERR_FAIL_RET_V( m_D3DDevice->CreatePipelineState( &pipeline_state_stream_desc, IID_PPV_ARGS( &pipeline ) ), {} );

  ToWideChar( wide_string_buf, desc.DebugName );
  ERR_FAIL_RET_V( pipeline->SetName( wide_string_buf ), nullptr );

  return pipeline;
}
