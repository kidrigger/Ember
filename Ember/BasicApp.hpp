#pragma once

#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "IApp.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class PerfCounter;
class RenderDevice;

class BasicApp final : public IApp
{
  struct Vertex
  {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Color;
  };

  HWND                          m_WindowHandle{ nullptr };
  uint32_t                      m_WindowWidth{ 1280 };
  uint32_t                      m_WindowHeight{ 720 };

  std::unique_ptr<RenderDevice> m_RenderDevice;
  std::unique_ptr<PerfCounter>  m_PerfCounter;
  wchar_t                       m_SprintfBuffer[1024]{};

  // Specifics
  ComPtr<ID3D12RootSignature> m_RootSignature;
  ComPtr<ID3D12PipelineState> m_PipelineState;

  DepthBuffer                 m_DepthBuffer;

  // Model Specific
  DirectX::XMMATRIX     m_GlobalTransform;

  std::vector<Vertex>   m_Vertices;
  std::vector<uint16_t> m_Indices;
  Buffer                m_VertexBuffer{};
  Buffer                m_IndexBuffer{};

public:
  BasicApp(
      HWND window_handle, std::unique_ptr<RenderDevice> render_device, std::unique_ptr<PerfCounter> perf_counter );

  void            LoadContent() override;
  void            Update() override;
  void            Render() override;
  void            UnloadContent() override;

  void            Resize() override;

  static BasicApp Create( HINSTANCE instance_handle );

  BasicApp( BasicApp const& other )                = delete;
  BasicApp& operator=( BasicApp const& other )     = delete;
  BasicApp( BasicApp&& other ) noexcept            = default;
  BasicApp& operator=( BasicApp&& other ) noexcept = default;
  ~BasicApp() override;
};

} // namespace Ember
