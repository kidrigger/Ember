#include "Camera.hpp"

#include <algorithm>

void Ember::Camera::UpdateRepr()
{
  if ( m_DirtyFlags & kViewDirtyBit )
  {
    DirectX::FXMVECTOR direction =
        XMVector3Rotate( kCameraFwd, DirectX::XMQuaternionRotationRollPitchYaw( m_Pitch, m_Yaw, 0.0f ) );
    m_Repr.View    = DirectX::XMMatrixLookToRH( m_Repr.Position, direction, kCameraUp );
    m_Repr.InvView = XMMatrixInverse( nullptr, m_Repr.View );
  }

  if ( m_DirtyFlags & kProjDirtyBit )
  {
    m_Repr.Projection =
        DirectX::XMMatrixPerspectiveFovRH( m_HorizontalFoV / m_AspectRatio, m_AspectRatio, 0.1f, 100.0f );
    m_Repr.InvProj = XMMatrixInverse( nullptr, m_Repr.Projection );
  }

  m_DirtyFlags = 0;
}

Ember::Camera::GpuRepr const& Ember::Camera::Repr()
{
  if ( m_DirtyFlags ) UpdateRepr();
  return m_Repr;
}

DirectX::FXMVECTOR& Ember::Camera::GetPosition() const
{
  return m_Repr.Position;
}

void Ember::Camera::SetPosition( DirectX::FXMVECTOR& position )
{
  m_Repr.Position = DirectX::XMVectorSetW( position, 1.0f );
}

void Ember::Camera::LocalTranslate( float const dx, float const dy, float const dz )
{
  auto delta = DirectX::XMVector3Rotate(
      DirectX::XMVectorSet( dx, dy, dz, 0.0f ), DirectX::XMQuaternionRotationRollPitchYaw( m_Pitch, m_Yaw, 0.0 ) );
  m_Repr.Position  = DirectX::XMVectorAdd( m_Repr.Position, delta );
  m_DirtyFlags    |= kViewDirtyBit;
}

float Ember::Camera::GetYaw() const
{
  return m_Yaw;
}

float Ember::Camera::GetPitch() const
{
  return m_Pitch;
}

void Ember::Camera::SetYawPitch( float const yaw, float const pitch )
{
  float constexpr static k85DegreeLimit  = DirectX::XMConvertToRadians( 85.0f );
  m_Yaw                                  = yaw;
  m_Pitch                                = std::clamp( pitch, -k85DegreeLimit, k85DegreeLimit );
  m_DirtyFlags                          |= kViewDirtyBit;
}

void Ember::Camera::SetAspectRatio( float const aspect_ratio )
{
  m_AspectRatio  = aspect_ratio;
  m_DirtyFlags  |= kProjDirtyBit;
}

void Ember::Camera::SetHorizontalFoV( float const fov )
{
  m_HorizontalFoV  = fov;
  m_DirtyFlags    |= kProjDirtyBit;
}
