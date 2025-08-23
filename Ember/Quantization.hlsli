#ifndef QUANTIZATION_HLSLI_
#define QUANTIZATION_HLSLI_

float4 UnpackR10G10B10A2( uint input )
{
  float4 result;
  result.r = ( input & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.g = ( ( input >> 10 ) & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.b = ( ( input >> 20 ) & ( ( 1 << 10 ) - 1 ) ) / 1023.0;
  result.a = ( ( input >> 30 ) & 0b11 ) / 3.0;
  return result;
}

#endif
