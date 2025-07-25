cbuffer BindlessIndex : register(b0, space0)
{
  uint g_CameraIndex;
  uint g_TextureIndex;
}

struct VSOut
{
  float4 Position : SV_POSITION;
  float4 Color : COLOR;
  float2 TexCoord : TEX_COORD0;
};

typedef VSOut FSIn;