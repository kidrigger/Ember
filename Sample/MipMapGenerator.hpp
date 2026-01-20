#pragma once

#include <Graphics/CommandList.hpp>
#include <Util/DirectXHeaders.hpp>

namespace Ember
{
class RenderDevice;
class Texture;

class MipMapGenerator
{
public:
  static constexpr uint32_t kMaxMipCount = 14;

  struct RootSigInfo
  {
    DirectX::XMFLOAT2 TexelSize;
    SRVHandle         Src;
    UAVHandle         Dst;
    uint32_t          SrcMipLevel;
    uint32_t          IsSrgb;
  };

private:
  RenderDevice*               m_RenderDevice;
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_Pipeline;
  ComPtr<ID3D12PipelineState> m_CubePipeline;

public:
  MipMapGenerator() = default;

  MipMapGenerator(
      RenderDevice*               render_device,
      ComPtr<ID3D12RootSignature> root_signature,
      ComPtr<ID3D12PipelineState> pipeline,
      ComPtr<ID3D12PipelineState> cube_pipeline );

  static bool Create( MipMapGenerator* generator, RenderDevice* render_device );

  bool        TryGenerateMipMaps( CommandList* command_list, Texture* texture ) const;
  bool        TryGenerateMipMapCube( CommandList* command_list, Texture* texture ) const;
};

} // namespace Ember
