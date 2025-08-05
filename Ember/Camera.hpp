#pragma once

#include "Util/DirectXHeaders.hpp"

namespace Ember
{

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
  };

private:
  DirectX::XMVECTORF32 constexpr static kCameraUp  = DirectX::XMVECTORF32{ 0.0f, 1.0f, 0.0f, 0.0f };
  DirectX::XMVECTORF32 constexpr static kCameraFwd = DirectX::XMVECTORF32{ 0.0f, 0.0f, -1.0f, 0.0f };
  uint32_t constexpr static kViewDirtyBit          = 0b0001;
  uint32_t constexpr static kProjDirtyBit          = 0b0010;

  GpuRepr  m_Repr;
  float    m_HorizontalFoV{ DirectX::XMConvertToRadians( 70.0f ) };
  float    m_AspectRatio{ 16.0f / 9.0f };
  float    m_Yaw{ 0 };
  float    m_Pitch{ 0 };
  uint32_t m_DirtyFlags{ UINT32_MAX };

  void     UpdateRepr();

public:
  [[nodiscard]] GpuRepr const&      Repr();
  [[nodiscard]] DirectX::FXMVECTOR& GetPosition() const;
  void                              SetPosition( DirectX::FXMVECTOR const& position );
  void                              LocalTranslate( float dx, float dy, float dz );
  float                             GetYaw() const;
  float                             GetPitch() const;
  void                              SetYawPitch( float yaw, float pitch );
  void                              SetAspectRatio( float aspect_ratio );
  void                              SetHorizontalFoV( float fov );
};
} // namespace Ember
