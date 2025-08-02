#pragma once

#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "IApp.hpp"
#include "RenderDevice.hpp"
#include "Scene.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class ModelLoader;
class PerfCounter;
class RenderDevice;

class BasicApp final : public IApp
{
  size_t constexpr static kMaxPointLights = 32;

  struct Camera
  {
    DirectX::XMMATRIX Projection{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX View{ DirectX::XMMatrixIdentity() };
    DirectX::XMVECTOR Position{ DirectX::XMVectorZero() };
  };

  struct PointLight
  {
    DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f }; // 12
    float             Range{ -1.0f };               // 16
    Color32           Color;                        // 20
    float             Intensity{ 1.0f };            // 24
    float             Attenuation{ 1.0f };          // 28
    float             Padding0{};                   // 32
  };

  HWND                          m_WindowHandle{ nullptr };
  uint32_t                      m_WindowWidth{ 1280 };
  uint32_t                      m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice> m_RenderDevice;
  std::unique_ptr<PerfCounter>  m_PerfCounter;
  std::unique_ptr<ModelLoader>  m_ModelLoader;
  wchar_t                       m_SprintfBuffer[1024]{};

  // Specifics
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_PipelineState;

  DepthBuffer                 m_DepthBuffer;

  Camera                      m_Camera;
  Buffer                      m_CameraBuffer[RenderDevice::kNumFrames];

  PointLight                  m_PointLights[kMaxPointLights];
  Buffer                      m_PointLightBuffer[RenderDevice::kNumFrames];
  uint32_t                    m_PointLightCount{ 0 };
  uint32_t                    m_PointLightDirty{ 3 };

  World                       m_World;
  RenderCommandQueue          m_RenderQueue;

public:
  BasicApp(
      HWND window_handle, std::unique_ptr<RenderDevice> render_device, std::unique_ptr<PerfCounter> perf_counter );

  void        LoadContent() override;
  void        Update() override;
  void        Render() override;
  void        UnloadContent() override;

  void        Resize() override;

  static void Create( BasicApp* app, HINSTANCE instance_handle );

  BasicApp( BasicApp const& other )                = delete;
  BasicApp& operator=( BasicApp const& other )     = delete;
  BasicApp( BasicApp&& other ) noexcept            = delete;
  BasicApp& operator=( BasicApp&& other ) noexcept = delete;
  ~BasicApp() override;
};

} // namespace Ember
