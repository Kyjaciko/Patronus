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

  const float3 gravity = float3(0.0f, -9.81f, 0.0f);

  Particle p = gParticles[i];
  p.vel += gravity * gDeltaTime;  // v(t) = v0 + a * t.
  p.pos += p.vel * gDeltaTime;    // x(t) = x0 + v * t.

  if (p.lifetime > 0.f) 
  {
    p.lifetime = max(0.f, p.lifetime - gDeltaTime);
  }

  gParticles[i] = p;
}