struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};

StructuredBuffer<Particle> gParticles   : register(t0);
Texture2D<float>           gDepthBuffer : register(t1);

cbuffer CameraCB : register(b0)
{
  row_major float4x4 gViewProj;

  float3 gCamRight;
  float  gBillboardSize;

  float3 gCamUp;
  float  gPad1;

  float gCamNear;
  float gCamFar;
  float gFadeDistance;
  float gPad2;
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

  // DrawInstanced(4, kParticleCount, 0, 0).
  //Particle p = gParticles[instanceID];
  //float2 corner = QuadCorners[vertexID];

  // DrawIndexedInstanced(6 * kParticleCount, 1, 0, 0, 0).
  Particle p = gParticles[vertexID / 4];
  float2 corner = QuadCorners[vertexID % 4];

  float3 worldPos = p.pos
                    + gCamRight * corner.x * gBillboardSize
                    + gCamUp    * corner.y * gBillboardSize;

  output.pos = mul(float4(worldPos, 1.f), gViewProj);

  float alpha = p.lifetime ? 1.f : 0.f;
  //output.color = float4(1.f * alpha, saturate(p.lifetime / 10.f) * alpha, 0.5f * alpha, alpha); // 10.f is starting value lifetime.
  //output.color = float4(0.f, alpha * length(p.pos) / 20.f, alpha * length(p.pos) / 10.f, alpha);
  output.color = float4(Hash(length(p.pos) / 10.f) * alpha, Hash(length(p.pos)) * alpha, Hash(length(p.pos) / 20.f) * alpha, alpha);

  return output;
}

// Reverse-Z depth!
float LinearizeDepth(float depth)
{
  return gCamNear * gCamFar / (depth * (gCamFar - gCamNear) + gCamNear);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
  float scene_z    = LinearizeDepth(gDepthBuffer.Load(int3(input.pos.xy, 0)));
  float particle_z = input.pos.w;

  float fade = saturate((scene_z - particle_z) / gFadeDistance);
  return input.color * fade;
}