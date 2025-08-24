#ifndef QUANTIZATION_HLSLI_
#define QUANTIZATION_HLSLI_

float4 UnpackR10G10B10A2Unorm( uint input )
{
  float4 result;
  result.r = ( input & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.g = ( ( input >> 10 ) & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.b = ( ( input >> 20 ) & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.a = ( ( input >> 30 ) & 0b11 ) / 3.0;
  return result;
}

float4 UnpackR10G10B10A2Snorm( uint input )
{
  float4 result;
  result.r = ( 2.0f * ( input & ( ( 1 << 10 ) - 1 ) ) - 1 ) / 511.0;
  result.g = ( 2.0f * ( ( input >> 10 ) & ( ( 1 << 10 ) - 1 ) ) - 1 ) / 511.0;
  result.b = ( 2.0f * ( ( input >> 20 ) & ( ( 1 << 10 ) - 1 ) ) - 1 ) / 511.0;
  result.a = ( 2.0f * ( ( input >> 30 ) & 0b11 ) - 1 );
  return result;
}

#endif
