#pragma once

#include "Util/DirectXHeaders.hpp"
#include "Util/HelperUtils.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

struct Color32
{
  uint8_t R{ 0 };
  uint8_t G{ 0 };
  uint8_t B{ 0 };
  uint8_t A{ 0 };

  Color32() = default;

  Color32( DirectX::XMVECTOR const& float_4 )
  {
    for ( int i = 0; i < 4; i++ )
    {
      ASSERT( float_4.m128_f32[0] >= 0.0f and float_4.m128_f32[0] <= 1.0f );
    }

    R = ( uint8_t )( float_4.m128_f32[0] * 255.99f );
    G = ( uint8_t )( float_4.m128_f32[1] * 255.99f );
    B = ( uint8_t )( float_4.m128_f32[2] * 255.99f );
    A = ( uint8_t )( float_4.m128_f32[3] * 255.99f );
  }

  Color32( DirectX::XMFLOAT4 const& float_4 )
  {
    ASSERT( float_4.x >= 0.0f and float_4.x <= 1.0f );
    ASSERT( float_4.y >= 0.0f and float_4.y <= 1.0f );
    ASSERT( float_4.z >= 0.0f and float_4.z <= 1.0f );
    ASSERT( float_4.w >= 0.0f and float_4.w <= 1.0f );

    R = ( uint8_t )( float_4.x * 255.99f );
    G = ( uint8_t )( float_4.y * 255.99f );
    B = ( uint8_t )( float_4.z * 255.99f );
    A = ( uint8_t )( float_4.w * 255.99f );
  }

  explicit operator UINT()
  {
    return *( UINT* )this;
  }
};

} // namespace Ember
