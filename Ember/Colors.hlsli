
typedef uint PackedColor32;

float4       UnpackColor32( PackedColor32 value )
{
  uint a = ( value & 0xFF000000 ) >> 24;
  uint b = ( value & 0x00FF0000 ) >> 16;
  uint g = ( value & 0x0000FF00 ) >> 8;
  uint r = value & 0x000000FF;
  return float4( r, g, b, a ) / 255.0f;
}

float3 SrgbToLinear( float3 color )
{
  return select( color < 0.04045f, color / 12.92, pow( ( color + 0.055 ) / 1.055, 2.4 ) );
}

float3 LinearToSrgb( float3 color )
{
  return select( color < 0.0031308, 12.92 * color, 1.055 * pow( abs( color ), 1.0 / 2.4 ) - 0.055 );
}
