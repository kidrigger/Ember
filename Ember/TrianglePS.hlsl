#include "Triangle.hlsli"

float4 TrianglePS(PSInput IN) : SV_TARGET0
{
  return IN.Color;
}
