#include "Compute.hlsli"

RWStructuredBuffer<uint> AliveList_In : register(u0, space3);
RWByteAddressBuffer      Arguments    : register(u0, space4);

cbuffer EmitterConstants : register(b1)
{
  uint emit_request;
  uint seed;
};

[numthreads(1,1,1)]
void Kickoff()
{
  const uint dead_count  = Counters.Load(DEAD);
  const uint emit_count  = min(emit_request, dead_count); // Dead count limits the particle count to the maximum.
  const uint alive_count = Counters.Load(ALIVE_OUT);

  // Update counters:
  // - How many dead particles left after emitting
  // - How many particles to emit
  // - How many are alive after emitting
  // - Reset how many particles were alive after the previous pass
  Counters.Store(DEAD,      dead_count - emit_count);
  Counters.Store(EMIT,      emit_count);
  Counters.Store(ALIVE_IN,  alive_count + emit_count);
  Counters.Store(ALIVE_OUT, 0u);

  // Update argument
  const uint emit_group_count = (emit_count + 63u) / 64u;
  Arguments.Store(X_EMIT, emit_group_count);
  Arguments.Store(Y_EMIT, 1u);
  Arguments.Store(Z_EMIT, 1u);

  const uint sim_group_count  = (alive_count + emit_count + 255u) / 256u;
  Arguments.Store(X_SIM,  sim_group_count);
  Arguments.Store(Y_SIM,  1u);
  Arguments.Store(Z_SIM,  1u);
}

[numthreads(64,1,1)]
void Emitter(uint3 id : SV_DispatchThreadID)
{
  const uint i = id.x;
  const uint emit_count = Counters.Load(EMIT);
  if (i >= emit_count) 
    return;

  const uint alive_index    = Counters.Load(ALIVE_IN) - emit_count + i;
  const uint particle_index = DeadList[Counters.Load(DEAD) + i];

  uint  random_seed = Hash(i + seed);
  float direction_x = (seed % 2u < 1u) ? 1.f : -1.f;
  float direction_y = (seed % 4u < 2u) ? 1.f : -1.f;
  float direction_z = (seed % 8u < 4u) ? 1.f : -1.f;
  float random_x    = Hash(random_seed + Hash(0u * seed)) / 4294967296.0f;
  float random_y    = Hash(random_seed + Hash(8u * seed)) / 4294967296.0f;
  float random_z    = Hash(random_seed + Hash(5u * seed)) / 4294967296.0f;

  Particle p;
  p.pos      = float3(0.f, 0.f, 0.f);
  p.vel      = float3(random_x * direction_x, random_y * direction_y, random_z * direction_z);
  p.lifetime = 20.f;
  p._pad     = 0.f;

  Particles[particle_index] = p;
  AliveList_In[alive_index] = particle_index;
}