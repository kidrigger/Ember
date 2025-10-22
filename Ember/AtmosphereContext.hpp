#pragma once

#include "Buffer.hpp"
#include "Color.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"
#include "fg/FrameGraphResource.hpp"

class FrameGraphBlackboard;
class FrameGraph;

namespace Ember
{
class RenderDevice;

class AtmosphereContext
{
public:
  struct OutData
  {
    FrameGraphResource TransmittanceLUT;
    FrameGraphResource SkyViewLUT;
    FrameGraphResource AtmosphereParams;
  };

  struct SunData
  {
    DirectX::XMFLOAT3 Direction{ 1.0f, 0.0f, 0.0f };
    Color32           Color{ Color32::White() };
    float             Intensity{ 12.8f };
  };

  struct Params
  {
    /* rayleigh */
    DirectX::XMFLOAT3 ScatterCoeffRayleigh{ 5.802e-6f, 13.558e-6f, 33.1e-6f }; // 12
    float             DensityFactorRayleigh{ 8000.0f };                        // 16

    /* ozone */
    DirectX::XMFLOAT3 AbsorptionCoeffOzone{ 0.65e-6f, 1.881e-6f, 0.085e-6f }; // 28
    float             OzoneHeight{ 25000.0f };                                // 32
    float             OzoneWidth{ 30000.0f };                                 // 36

    /* mei */
    float ScatterCoeffMei{ 3.996e-6f };  // 40
    float AbsorptionCoeffMei{ 4.4e-6f }; // 44
    float DensityFactorMei{ 1200.0f };   // 48

    float AsymmetryMei{ 0.8f };          // 52

    /* sampling */
    int      DepthSamples{ 400 }; // 56
    int      ViewSamples{ 300 };  // 60

    uint32_t Padding{ 0 };        // 64
  };

private:
  DirectX::XMUINT2 constexpr static kTransmittanceLUTSize = { 64, 256 };
  DXGI_FORMAT constexpr static kTransmittanceLUTFormat    = DXGI_FORMAT_R11G11B10_FLOAT;
  DirectX::XMUINT2 constexpr static kSkyViewLUTSize       = { 256, 128 };
  DXGI_FORMAT constexpr static kSkyViewLUTFormat          = DXGI_FORMAT_R11G11B10_FLOAT;

  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_TransmittanceLUTPipeline;
  ComPtr<ID3D12PipelineState> m_SkyViewLUTPipeline;

  ComPtr<ID3D12RootSignature> m_AerialPerspectiveRootSignature;
  ComPtr<ID3D12PipelineState> m_AerialPerspectivePipeline;

  Texture                     m_TransmittanceLUT;
  Texture                     m_SkyViewLUT;
  std::vector<Buffer>         m_AtmosphereParamBuffers;

  Params                      m_AtmosphereParams;
  uint32_t                    m_Sun{ 0 };
  uint8_t                     m_LUTUpdatePendingFrames;

public:
  AtmosphereContext() = default;
  AtmosphereContext(
      ComPtr<ID3D12RootSignature> root_signature,
      ComPtr<ID3D12PipelineState> transmittance_lut_pipeline,
      ComPtr<ID3D12PipelineState> sky_view_lut_pipeline,
      ComPtr<ID3D12RootSignature> aerial_perspective_root_signature,
      ComPtr<ID3D12PipelineState> aerial_perspective_pipeline,
      Texture                     transmittance_lut,
      Texture                     sky_view_lut,
      std::vector<Buffer>         atmosphere_param_buffers );

  static bool                 Create( AtmosphereContext* out, RenderDevice* render_device );

  OutData                     Render( FrameGraph* frame_graph, FrameGraphBlackboard* blackboard, uint32_t frame_idx );

  void                        SetSun( uint32_t sun_index );
  void                        SetAtmosphereParams( Params const& atmosphere_params );
  [[nodiscard]] Params const& GetAtmosphereParams() const;
};

} // namespace Ember
