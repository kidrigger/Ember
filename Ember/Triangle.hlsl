struct VSOutput
{
  float4 Color : COLOR;
  float4 Position : SV_Position;
};

struct PSInput
{
  float4 Color : COLOR;
};

const static float3 kPoints[] =
{
  float3(-0.5f, -0.5f, 0.0f),
  float3(0.5f, -0.5f, 0.0f),
  float3(0.0f, 0.5f, 0.0f),
};

const static float3 kColors[] =
{
  float3(1.0f, 0.0f, 0.0f),
  float3(0.0f, 1.0f, 0.0f),
  float3(0.0f, 0.0f, 1.0f),
};

VSOutput VertexMain(uint idx : SV_VERTEXID)
{
  VSOutput OUT;
  OUT.Position = float4(kPoints[idx], 1.0f);
  OUT.Color = float4(kColors[idx], 1.0f);
  return OUT;
}

float4 PixelMain(PSInput IN) : SV_TARGET0
{
  return IN.Color;
}
