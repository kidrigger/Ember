#pragma once

#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "IApp.hpp"
#include "Scene.hpp"
#include "Texture.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class PerfCounter;
class RenderDevice;
class TextureLoader;

class BasicApp final : public IApp
{
  struct Vertex
  {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Color;
    DirectX::XMFLOAT2 TexCoord0;
  };

  struct Camera
  {
    DirectX::XMMATRIX Projection{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX View{ DirectX::XMMatrixIdentity() };
  };

  HWND                           m_WindowHandle{ nullptr };
  uint32_t                       m_WindowWidth{ 1280 };
  uint32_t                       m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice>  m_RenderDevice;
  std::unique_ptr<PerfCounter>   m_PerfCounter;
  std::unique_ptr<TextureLoader> m_TextureLoader;
  wchar_t                        m_SprintfBuffer[1024]{};

  // Specifics
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_PipelineState;

  DepthBuffer                 m_DepthBuffer;

  Camera                      m_Camera;
  Buffer                      m_CameraBuffer;

  World                       m_World;
  RenderCommandQueue          m_RenderQueue;

public:
  BasicApp(
      HWND                           window_handle,
      std::unique_ptr<RenderDevice>  render_device,
      std::unique_ptr<PerfCounter>   perf_counter,
      std::unique_ptr<TextureLoader> texture_loader );

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
