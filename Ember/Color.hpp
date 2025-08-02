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
  uint8_t A{ UINT8_MAX }; // Alpha should not be zero by default.

  Color32() = default;

  constexpr Color32( DirectX::XMVECTOR const& float_4 )
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

  constexpr Color32( DirectX::XMFLOAT4 const& float_4 )
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

  constexpr Color32( float const r, float const g, float const b, float const a )
  {
    ASSERT( r >= 0.0f and r <= 1.0f );
    ASSERT( g >= 0.0f and g <= 1.0f );
    ASSERT( b >= 0.0f and b <= 1.0f );
    ASSERT( a >= 0.0f and a <= 1.0f );

    R = ( uint8_t )( r * 255.99f );
    G = ( uint8_t )( g * 255.99f );
    B = ( uint8_t )( b * 255.99f );
    A = ( uint8_t )( a * 255.99f );
  }

  constexpr static Color32 Black()
  {
    return {};
  }

  constexpr static Color32 White()
  {
    return { 1.0f, 1.0f, 1.0f, 1.0f };
  }

  constexpr static Color32 Red()
  {
    return { 1.0f, 0.0f, 0.0f, 1.0f };
  }

  constexpr static Color32 Green()
  {
    return { 0.0f, 1.0f, 0.0f, 1.0f };
  }

  constexpr static Color32 Blue()
  {
    return { 0.0f, 0.0f, 1.0f, 1.0f };
  }

  explicit operator UINT()
  {
    return *( UINT* )this;
  }
};

} // namespace Ember
