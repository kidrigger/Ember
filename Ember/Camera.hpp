#pragma once

#include "Buffer.hpp"
#include "Util/DirectXHeaders.hpp"

namespace Ember
{
class RenderDevice;

class Camera
{
public:
  struct GpuRepr
  {
    DirectX::XMMATRIX Projection{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX InvProj{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX View{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX InvView{ DirectX::XMMatrixIdentity() };
    DirectX::XMVECTOR Position{ DirectX::XMVectorZero() };
    DirectX::XMFLOAT4 FrustumInfo{ 1.0f, 1.0f, 0.1f, 100.0f };
  };

private:
  DirectX::XMVECTORF32 constexpr static kCameraUp  = DirectX::XMVECTORF32{ 0.0f, 1.0f, 0.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kCameraFwd = DirectX::XMVECTORF32{ 0.0f, 0.0f, -1.0f, 0.0f };
  uint32_t constexpr static kViewDirtyBit          = 0b0001;
  uint32_t constexpr static kProjDirtyBit          = 0b0010;

  std::vector<Buffer>      m_CameraBuffer;
  GpuRepr                  m_Repr;
  DirectX::BoundingFrustum m_Frustum;
  CBVHandle                m_LastFrameHandle;
  float                    m_HorizontalFoV{ DirectX::XMConvertToRadians( 70.0f ) };
  float                    m_AspectRatio{ 16.0f / 9.0f };
  float                    m_Yaw{ 0 };
  float                    m_Pitch{ 0 };
  uint32_t                 m_DirtyFlags{ UINT32_MAX };

public:
  Camera() = default;
  explicit Camera( std::vector<Buffer> camera_buffer );

  static void Create( Camera* camera, RenderDevice* render_device, uint32_t num_frames );

  //
  [[nodiscard]] DirectX::XMVECTOR const& GetPosition() const;
  void                                   SetPosition( DirectX::FXMVECTOR const& position );
  void                                   LocalTranslate( float dx, float dy, float dz );
  [[nodiscard]] float                    GetYaw() const;
  [[nodiscard]] float                    GetPitch() const;
  void                                   SetYawPitch( float yaw, float pitch );
  void                                   SetAspectRatio( float aspect_ratio );
  void                                   SetHorizontalFoV( float fov );

  [[nodiscard]] DirectX::XMMATRIX const& GetView() const;
  [[nodiscard]] DirectX::XMMATRIX const& GetInvView() const;
  [[nodiscard]] DirectX::XMMATRIX const& GetProj() const;
  [[nodiscard]] DirectX::XMMATRIX const& GetInvProj() const;

  void                                   UpdateRepr();
  CBVHandle                              PrepareFrame( uint32_t frame_index );
  [[nodiscard]] CBVHandle                GetLastUpdatedBuffer() const;
  DirectX::BoundingFrustum const&        GetLastUpdatedFrustum() const;
};
} // namespace Ember
