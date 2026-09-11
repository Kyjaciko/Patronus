struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};

RWStructuredBuffer<Particle> gParticles : register(u0);

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
  uint i = id.x;
  if (i >= 500) // kParticleCount
    return;

  Particle p = gParticles[i];
  p.pos += p.vel;
  if (p.lifetime > 0.f) p.lifetime -= 1.f; // For now a constant, should be deltaTime.

  gParticles[i] = p;
}