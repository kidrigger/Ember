float3 SrgbToLinear( float3 color )
{
  return select( color < 0.04045f, color / 12.92, pow( ( color + 0.055 ) / 1.055, 2.4 ) );
}

float3 LinearToSrgb( float3 color )
{
  return select( color < 0.0031308, 12.92 * color, 1.055 * pow( abs( color ), 1.0 / 2.4 ) - 0.055 );
}
