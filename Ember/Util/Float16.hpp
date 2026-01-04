#pragma once

#include <cstdint>

namespace Ember
{

class Float16
{
  uint16_t                  m_Value;

  constexpr static uint16_t QuantizeFloat16( float const value )
  {
    // Copied from Meshoptimizer with name changes.
    union
    {
      float    F;
      uint32_t ByteMuck;
    } const u         = { value };
    uint32_t const ui = u.ByteMuck;

    int const      s  = ( int )( ( ui >> 16 ) & 0x8000 );
    int const      em = ( int )( ui & 0x7fffffff );

    // bias exponent and round to nearest; 112 is relative exponent bias (127-15)
    int h = ( em - ( 112 << 23 ) + ( 1 << 12 ) ) >> 13;

    // underflow: flush to zero; 113 encodes exponent -14
    h = ( em < ( 113 << 23 ) ) ? 0 : h;

    // overflow: infinity; 143 encodes exponent 16
    h = ( em >= ( 143 << 23 ) ) ? 0x7c00 : h;

    // NaN; note that we convert all types of NaN to qNaN
    h = ( em > ( 255 << 23 ) ) ? 0x7e00 : h;

    return ( uint16_t )( s | h );
  }

public:
  Float16() = default;

  constexpr Float16( float const value ) : m_Value( QuantizeFloat16( value ) )
  {}
};

} // namespace Ember
