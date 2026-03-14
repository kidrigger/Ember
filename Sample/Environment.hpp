#pragma once
#include <vector>

#include <Graphics/DeviceHandle.hpp>
#include <Graphics/RenderDevice.hpp>
#include <Graphics/Texture.hpp>
#include <Util/Float16.hpp>

#include "Util/SpatialHashMap.hpp"
#include "fg/Blackboard.hpp"

namespace Ember
{
class MipMapGenerator;
class TextureLoader;
class World;

struct ReflectionProbe
{
  float Radius{ 1.0f };
};

class Environment
{
public:
  constexpr static uint32_t    kEnvCubeSide             = 128;
  constexpr static uint32_t    kDiffuseCubeSide         = 32;
  constexpr static uint32_t    kPrefilterCubeSide       = 64;
  constexpr static uint32_t    kPrefilterMaxLoD         = 5;
  constexpr static uint32_t    kBrdfLUTSize             = 256;

  constexpr static DXGI_FORMAT kProbeRenderTargetFormat = DXGI_FORMAT_R11G11B10_FLOAT;
  constexpr static DXGI_FORMAT kProbeDepthFormat        = DXGI_FORMAT_D16_UNORM;

  struct alignas( 16 ) GpuRepr
  {
    SRVHandle Skybox;
    SRVHandle DiffuseIrradiance;
    SRVHandle Prefilter;
    SRVHandle BrdfLUT;
    SRVHandle ReflectionProbes;
    SRVHandle CellProbeMap;
    uint32_t  CellProbeMapSlotCount;
    float     CellSize;
  };

  struct ReflectionProbeRepr
  {
    Float16   Position[3];
    Float16   Radius;
    SRVHandle Prefilter;
  };

  struct IBLEnvironment
  {
    Texture Skybox;
    Texture DiffuseIrradiance;
    Texture Prefilter;
  };

  struct Pipelines
  {
    // IBL Specific
    ComPtr<ID3D12RootSignature> IBLRootSignature;
    ComPtr<ID3D12PipelineState> EqRectToCubePipeline;
    ComPtr<ID3D12PipelineState> DiffuseIrradiance;
    ComPtr<ID3D12PipelineState> Prefilter;
    ComPtr<ID3D12PipelineState> BrdfLUT;

    // Probe Capture
    ComPtr<ID3D12RootSignature> ProbeRootSignature;
    ComPtr<ID3D12PipelineState> ProbePipeline;
  };

  struct LoadFromFile
  {
    RenderDevice*  RenderDevice;
    World*         World;
    TextureLoader* TextureLoader;
    char const*    FileName;
  };

  struct LoadFromEqRect
  {
    RenderDevice*    RenderDevice;
    World*           World;
    MipMapGenerator* MipMapper;
    Texture          EqrectTexture;
  };

  struct LoadFromCube
  {
    RenderDevice* RenderDevice;
    World*        World;
    Texture       CubeTexture;
  };

private:
  RenderDevice*        m_RenderDevice{ nullptr };
  World*               m_World{ nullptr };
  IBLEnvironment       m_FallbackIBL;
  std::vector<Texture> m_ReflectionProbeTextures;
  Buffer               m_ReflectionProbeBuffer;
  Buffer               m_CellProbeMapBuffer;
  Pipelines            m_Pipelines;
  Texture              m_BrdfLUT;
  GpuRepr              m_Repr;

public:
  Environment() = default;

  Environment( RenderDevice* render_device, World* world, IBLEnvironment ibl, Pipelines pipelines, Texture brdf_lut );

  [[nodiscard]] GpuRepr const& Repr() const;

  bool Bake( CommandList* command_list, MipMapGenerator* mipmapper, FrameGraphBlackboard const& blackboard );

  //
  static bool TryLoadFromFile( Environment* env, LoadFromFile const& args );
  static bool TryLoadFromEqRect( Environment* env, LoadFromEqRect const& args );
  static bool TryLoadFromCube( Environment* env, LoadFromCube const& args );
};

} // namespace Ember
