#ifndef CAMERA_HLSLI_
#define CAMERA_HLSLI_

struct Camera
{
  float4x4 Projection;
  float4x4 InvProj;
  float4x4 View;
  float4x4 InvView;
  float4   Position;
  float4   CullInfo; // x = h_slope, y = v_slope, z = near, w = far
};

#endif
