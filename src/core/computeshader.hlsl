struct Particle
{
  float3 pos;
  float _pad;
};

RWStructuredBuffer<Particle> gParticles : register(u0);

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
  uint i = id.x;
  if (i >= 500000) // kParticleCount
    return;

  Particle p = gParticles[i];

  float3 velocity = { 1.5f, 0.5f, 1.f };
  p.pos += velocity * 0.01f;

  gParticles[i] = p;
}