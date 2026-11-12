static const uint DEAD = 0u;
static const uint EMIT = 4u; // In bytes (uint = 32 bit = 4 bytes).

struct Particle
{
  float3 pos;
  float3 vel;
  float  lifetime;
  float  _pad;
};

RWStructuredBuffer<Particle> Particles : register(u0, space0);
RWStructuredBuffer<uint>     DeadList  : register(u0, space1);
RWByteAddressBuffer          Counters  : register(u0, space2);

cbuffer EmitterConstants : register(b0)
{
  uint emit_request;
  uint seed;
};

[numthreads(1,1,1)]
void Kickoff()
{
  const uint dead_count = Counters.Load(DEAD);
  const uint emit_count = min(emit_request, dead_count);

  Counters.Store(EMIT, emit_count);
  Counters.Store(DEAD, dead_count - emit_count);
}

uint Hash(uint v)
{
  uint state = v * 747796405u + 2891336453u;
  uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

[numthreads(64,1,1)]
void Emitter(uint3 id : SV_DispatchThreadID)
{
  if (id.x >= Counters.Load(EMIT)) 
    return;

  uint index  = DeadList[Counters.Load(DEAD) + id.x];

  uint  random_seed = Hash(id.x + seed);
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

  Particles[index] = p;
}