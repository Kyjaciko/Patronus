struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};

StructuredBuffer<Particle> gParticles : register(t0);

cbuffer CameraCB : register(b0)
{
  row_major float4x4 gViewProj;

  float3 gCamRight;
  float gBillboardSize;

  float3 gCamUp;
  float gPad;
};

static const float2 QuadCorners[4] = 
{
  float2(-1.f, 1.f),
  float2(1.f, 1.f),
  float2(-1.f, -1.f),
  float2(1.f, -1.f)
};

struct VSOutput
{
  float4 pos : SV_POSITION;
  float4 color : COLOR;
};

float Hash(uint seed)
{
  seed = (seed ^ 61u) ^ (seed >> 16u);
  seed *= 9u;
  seed = seed ^ (seed >> 4u);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15u);
  return float(seed) / 4294967295.f;
}

VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
  VSOutput output;

  Particle p = gParticles[instanceID];

  float2 corner = QuadCorners[vertexID];
  float3 worldPos = p.pos
                    + gCamRight * corner.x * gBillboardSize
                    + gCamUp    * corner.y * gBillboardSize;

  output.pos = mul(float4(worldPos, 1.f), gViewProj);

  float alpha = p.lifetime ? 1.f : 0.f; // Blend state still needs to be setup to work.
  //output.color = float4(1.f, saturate(p.lifetime / 10.f), 0.5f, alpha); // 10.f is starting value lifetime.
  //output.color = float4(0.f, sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z) / 20.f, sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z) / 10.f, alpha);
  output.color = float4(Hash(sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z) / 10.f), Hash(sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z)), Hash(sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z) / 20.f), alpha);

  return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
  return input.color;
}