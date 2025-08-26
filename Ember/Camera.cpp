#include "Camera.hpp"

#include <algorithm>

#include "RenderDevice.hpp"

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
    m_Repr.InvProj    = XMMatrixInverse( nullptr, m_Repr.Projection );

    m_Repr.CullInfo.x = std::tan( m_HorizontalFoV * 0.5f );
    m_Repr.CullInfo.y = std::tan( m_HorizontalFoV / m_AspectRatio * 0.5f );
    m_Repr.CullInfo.z = 0.1f;
    m_Repr.CullInfo.w = 100.0f;
  }

  m_DirtyFlags = 0;
}

Ember::Camera::Camera( std::vector<Buffer> camera_buffer ) : m_CameraBuffer{ std::move( camera_buffer ) }
{}

void Ember::Camera::Create( Camera* camera, RenderDevice* render_device, uint32_t num_frames )
{
  std::vector<Buffer> buffers;
  for ( uint32_t i = 0; i < num_frames; i++ )
  {
    buffers.push_back( render_device->CreateConstantBuffer( sizeof( GpuRepr ) ) );
  }
  new ( camera ) Camera{ std::move( buffers ) };
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

Ember::CBVHandle Ember::Camera::PrepareFrame( uint32_t const frame_index )
{
  UpdateRepr();

  m_CameraBuffer[frame_index].Write( 0, sizeof( m_Repr ), &m_Repr );

  return m_CameraBuffer[frame_index].GetCBVHandle();
}

DirectX::BoundingFrustum Ember::Camera::GetLastUpdatedFrustum() const
{
  DirectX::BoundingFrustum frustum;
  DirectX::BoundingFrustum frustum_out;
  DirectX::BoundingFrustum::CreateFromMatrix( frustum, m_Repr.Projection, true );
  frustum.Transform( frustum_out, m_Repr.InvView );
  return frustum_out;
}
