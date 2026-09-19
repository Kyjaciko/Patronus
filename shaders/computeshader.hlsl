struct Particle
{
  float3 pos;
  float3 vel;
  float lifetime;
  float _pad;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
Texture3D<float4> gCurlNoiseTexture : register(t0);
SamplerState gSampler : register(s0, space0);

cbuffer SimulationConstants : register(b0)
{
  float gDeltaTime;
  uint gParticleCount;
}

// Semi-implicit Euler.
/*[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
  uint i = id.x;
  if (i >= gParticleCount)
    return;

  const float3 gravity = float3(0.f, -9.81f, 0.f);

  Particle p = gParticles[i];
  p.vel += gravity * gDeltaTime;  // v(t) = v0 + a * t.
  p.pos += p.vel * gDeltaTime;    // x(t) = x0 + v * t.

  if (p.lifetime > 0.f) 
  {
    p.lifetime = max(0.f, p.lifetime - gDeltaTime);
  }

  gParticles[i] = p;
}*/

// Curl noise.
[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
  uint i = id.x;
  if (i >= gParticleCount)
    return;

  Particle p = gParticles[i];

  // Tweak this value for faster or slower curls.
  const float curlStrength = 100.f;

  // Map position in the world to UVW within [0.0, 1.0].
  const float3 worldMin = float3(-10.f, -10.f, -10.f);  // See: tools//curl noise generator//curl_noise_64x64x64_rgba16f.json.
  const float3 worldSize = float3(20.f, 20.f, 20.f);    // Max - Min.

  float3 uvw = (p.pos - worldMin) / worldSize;

  // Apply the curl force to the particles.
  float3 curlForce = gCurlNoiseTexture.SampleLevel(gSampler, uvw, 0).xyz;
  p.vel += curlForce * curlStrength * gDeltaTime;

  // Apply gravity.
  //const float3 gravity = float3(0.f, -9.81f, 0.f);
  //p.vel += gravity * gDeltaTime;  // v(t) = v0 + a * t.

  // Only activate when using 'Uniformly distributed sphere placement'.
  {
    // Keeps particles within a certain radius.
    const float3 sphereCentrum = float3(0.f, 0.f, 0.f);
    const float targetRadius = 10.f;
    const float bandThickness = 5.f;
    const float pullStrength = 40.f;

    const float innerRadius = targetRadius - 0.5f * bandThickness;
    const float outerRadius = targetRadius + 0.5f * bandThickness;

    float3 toCenter = sphereCentrum - p.pos;
    float distToCenter = length(toCenter);
    float3 directionToCenter = (distToCenter > 1e-5f) ? (toCenter / distToCenter) : float3(0.f, 0.f, 0.f);

    float error = (distToCenter > outerRadius) ? (distToCenter - outerRadius) : (distToCenter - innerRadius);
    p.vel += directionToCenter * error * pullStrength * gDeltaTime;
  }

  // Apply damping, prevents vectors from causing the speed to increase infinitely.
  p.vel *= 1.0f - saturate(5.f * gDeltaTime); // Lose 500% of speed each second.

  // Update particle position.
  p.pos += p.vel * gDeltaTime;    // x(t) = x0 + v * t.

  if (p.lifetime > 0.f) 
  {
    p.lifetime = max(0.f, p.lifetime - gDeltaTime);
  }

  gParticles[i] = p;
}