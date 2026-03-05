#pragma once
#include <Graphics/DeviceHandle.hpp>
#include <Graphics/RenderDevice.hpp>
#include <Graphics/Texture.hpp>

namespace Ember
{
class MipMapGenerator;

class TextureLoader;

class Environment
{
public:
  uint32_t constexpr static kEnvCubeSide       = 512;
  uint32_t constexpr static kDiffuseCubeSide   = 256;
  uint32_t constexpr static kPrefilterCubeSide = 512;
  uint32_t constexpr static kPrefilterMaxLoD   = 5;
  uint32_t constexpr static kBrdfLUTSize       = 512;

  struct alignas( 16 ) GpuRepr
  {
    SRVHandle Skybox;
    SRVHandle DiffuseIrradiance;
    SRVHandle Prefilter;
    SRVHandle BrdfLUT;
  };

  struct IBLEnvironment
  {
    Texture Skybox;
    Texture DiffuseIrradiance;
    Texture Prefilter;
  };

  struct Pipelines
  {
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> EqRectToCubePipeline;
    ComPtr<ID3D12PipelineState> DiffuseIrradiance;
    ComPtr<ID3D12PipelineState> Prefilter;
    ComPtr<ID3D12PipelineState> BrdfLUT;
  };

  struct LoadFromFile
  {
    RenderDevice*  RenderDevice;
    TextureLoader* TextureLoader;
    char const*    FileName;
  };

  struct LoadFromEqRect
  {
    RenderDevice*    RenderDevice;
    MipMapGenerator* MipMapper;
    Texture          EqrectTexture;
  };

  struct LoadFromCube
  {
    RenderDevice* RenderDevice;
    Texture       CubeTexture;
  };

private:
  IBLEnvironment m_FallbackIBL;
  Pipelines      m_Pipelines;
  Texture        m_BrdfLUT;
  GpuRepr        m_Repr;

public:
  Environment() = default;

  Environment( IBLEnvironment ibl, Pipelines pipelines, Texture brdf_lut );

  [[nodiscard]] GpuRepr const& Repr() const;

  //
  static bool TryLoadFromFile( Environment* env, LoadFromFile const& args );
  static bool TryLoadFromEqRect( Environment* env, LoadFromEqRect const& args );
  static bool TryLoadFromCube( Environment* env, LoadFromCube const& args );
};

} // namespace Ember
