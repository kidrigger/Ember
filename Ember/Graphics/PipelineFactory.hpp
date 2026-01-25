#pragma once

#include <optional>
#include <span>
#include <string>
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class RenderDevice;

struct RootConstants
{
  uint32_t                Register;
  uint32_t                SizeBytes;
  uint32_t                Space      = 0;
  D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL;

  operator D3D12_ROOT_PARAMETER1() const noexcept;
};

struct RootConstantBuffer
{
  uint32_t                    Register;
  uint32_t                    Space      = 0;
  D3D12_ROOT_DESCRIPTOR_FLAGS Flags      = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
  D3D12_SHADER_VISIBILITY     Visibility = D3D12_SHADER_VISIBILITY_ALL;

  operator D3D12_ROOT_PARAMETER1() const noexcept;
};

struct Rasterizer
{
  enum class FrontFace
  {
    kClockwise,
    kCounterClockwise,
  };

  enum class CullMode
  {
    kNone,
    kFront,
    kBack,
  };

  FrontFace FrontFace = FrontFace::kCounterClockwise;
  CullMode  CullMode  = CullMode::kBack;
};

struct GraphicsPipelineDesc
{
  ID3D12RootSignature*                      RootSignature;
  std::span<DXGI_FORMAT const>              RTVFormats = {};
  std::optional<Rasterizer>                 RasterizerDesc;
  std::optional<CD3DX12_DEPTH_STENCIL_DESC> DepthStencilDesc;
  std::optional<CD3DX12_BLEND_DESC>         BlendDesc;
  std::string                               VertexShaderName;
  std::string                               AmpShaderName;
  std::string                               MeshShaderName;
  std::string                               PixelShaderName;
  std::optional<DXGI_FORMAT>                DSVFormat;
  std::string                               DebugName;
};

struct RootSignatureDesc
{
  enum class Access
  {
    kVertexPixel = D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS,
    kAmpMeshPixel = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS,
    kMeshPixel    = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS,
  };

  std::span<D3D12_ROOT_PARAMETER1>     RootParameters;
  std::span<D3D12_STATIC_SAMPLER_DESC> StaticSamplers;
  Access                               ShaderAccess = Access::kAmpMeshPixel;
  std::string                          DebugName;
};

class Pipeline
{
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_Pipeline;

public:
  Pipeline() = default;
  Pipeline( ComPtr<ID3D12RootSignature> root_signature, ComPtr<ID3D12PipelineState> pipeline );
  [[nodiscard]] ID3D12RootSignature* GetRootSignature() const noexcept;
  [[nodiscard]] ID3D12PipelineState* GetPipeline() const noexcept;
};

class PipelineFactory
{
  ComPtr<ID3D12Device5>      m_D3DDevice;
  D3D_ROOT_SIGNATURE_VERSION m_RootSignatureVersion;

public:
  PipelineFactory() = default;
  explicit PipelineFactory( ComPtr<ID3D12Device5> d3d_device, D3D_ROOT_SIGNATURE_VERSION root_signature_version );

  [[nodiscard]] ComPtr<ID3D12RootSignature> CreateRootSignature( RootSignatureDesc const& desc ) const;

  [[nodiscard]] ComPtr<ID3D12PipelineState> CreateGraphicsPipeline( GraphicsPipelineDesc const& desc ) const;
};

} // namespace Ember
