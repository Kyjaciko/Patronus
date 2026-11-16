#include "Compute.hlsli"

static const float3 gravity = float3(0.f, 0.f, 0.f);

StructuredBuffer<uint>   AliveList_IN  : register(t0, space0);
RWStructuredBuffer<uint> AliveList_OUT : register(u0, space3);

cbuffer SimulationConstants : register(b0)
{
  float delta_time;
}

[numthreads(256, 1, 1)]
void Simulate(uint3 id : SV_DispatchThreadID)
{
  const uint i     = id.x;
  const uint count = Counters.Load(ALIVE_IN);
  if (i >= count)
    return;

  const uint particler_index = AliveList_IN[i];
  Particle p = Particles[particler_index];
  if (p.lifetime <= 0.f)
    return;

  uint alive_index;
  p.lifetime -= delta_time;
  if (p.lifetime <= 0.f)
  {
    Counters.InterlockedAdd(DEAD, 1u, alive_index);
    DeadList[alive_index] = particler_index;
    return;
  }

  // Semi-implicit Euler.
  p.vel += gravity * delta_time;  // v(t) = v0 + a * t.
  p.pos += p.vel   * delta_time;  // x(t) = x0 + v * t.

  Counters.InterlockedAdd(ALIVE_OUT, 1u, alive_index);
  Particles[particler_index] = p;
  AliveList_OUT[alive_index] = particler_index;
}