struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};

RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer SimulationConstants : register(b0)
{
  float gDeltaTime;
  uint gParticleCount;
}

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
  uint i = id.x;
  if (i >= gParticleCount)
    return;

  Particle p = gParticles[i];
  p.pos += p.vel * gDeltaTime;
  if (p.lifetime > 0.f) p.lifetime -= gDeltaTime;

  gParticles[i] = p;
}