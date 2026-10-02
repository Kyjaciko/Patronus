struct PSInput
{
  float4 position : SV_POSITION;
  float4 color : COLOR;
};

cbuffer CameraCB : register(b0)
{
  row_major float4x4 gViewProj;
};

PSInput VSMain(float3 position : POSITION, float4 color : COLOR)
{
  PSInput result;

  result.position = mul(float4(position, 1.f), gViewProj);
  result.color    = color;

  return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
  return input.color;
}