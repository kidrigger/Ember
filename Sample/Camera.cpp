#include "Camera.hpp"

#include <algorithm>
#include <cmath>

#include <Graphics/RenderDevice.hpp>

void Ember::Camera::Update()
{
  if ( m_DirtyFlags & kViewDirtyBit )
  {
    DirectX::FXMVECTOR direction =
        XMVector3Rotate( kCameraFwd, DirectX::XMQuaternionRotationRollPitchYaw( m_Pitch, m_Yaw, 0.0f ) );
    m_Repr.View    = XMMatrixLookToRH( m_Repr.Position, direction, kCameraUp );
    m_Repr.InvView = XMMatrixInverse( nullptr, m_Repr.View );
  }

  if ( m_DirtyFlags & kProjDirtyBit )
  {
    m_Repr.Projection =
        DirectX::XMMatrixPerspectiveFovRH( m_HorizontalFoV / m_AspectRatio, m_AspectRatio, 0.1f, 1000.0f );
    m_Repr.InvProj       = XMMatrixInverse( nullptr, m_Repr.Projection );

    m_Repr.FrustumInfo.x = std::tan( m_HorizontalFoV * 0.5f );
    m_Repr.FrustumInfo.y = std::tan( m_HorizontalFoV / m_AspectRatio * 0.5f );
    m_Repr.FrustumInfo.z = 0.1f;
    m_Repr.FrustumInfo.w = 1000.0f;
  }

  if ( m_DirtyFlags )
  {
    DirectX::BoundingFrustum frustum;
    DirectX::BoundingFrustum::CreateFromMatrix( frustum, m_Repr.Projection, true );
    frustum.Transform( m_Frustum, m_Repr.InvView );
  }

  m_DirtyFlags = 0;
}

DirectX::FXMVECTOR& Ember::Camera::GetPosition() const
{
  return m_Repr.Position;
}

void Ember::Camera::SetPosition( DirectX::FXMVECTOR& position )
{
  m_Repr.Position  = DirectX::XMVectorSetW( position, 1.0f );
  m_DirtyFlags    |= kViewDirtyBit;
}

void Ember::Camera::SetPosition( float const x, float const y, float const z )
{
  m_Repr.Position  = DirectX::XMVectorSet( x, y, z, 1.0f );
  m_DirtyFlags    |= kViewDirtyBit;
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

DirectX::XMMATRIX const& Ember::Camera::GetView() const
{
  return m_Repr.View;
}

DirectX::XMMATRIX const& Ember::Camera::GetInvView() const
{
  return m_Repr.InvView;
}

DirectX::XMMATRIX const& Ember::Camera::GetProj() const
{
  return m_Repr.Projection;
}

DirectX::XMMATRIX const& Ember::Camera::GetInvProj() const
{
  return m_Repr.InvProj;
}

Ember::Camera::GpuRepr const& Ember::Camera::GetGpuRepr() const
{
  return m_Repr;
}

DirectX::BoundingFrustum const& Ember::Camera::GetLastUpdatedFrustum() const
{
  return m_Frustum;
}
