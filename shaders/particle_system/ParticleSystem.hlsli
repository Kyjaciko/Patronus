#pragma once

// Counters
static const uint DEAD      = 0u;
static const uint EMIT      = 4u;  // In bytes (uint = 32 bit = 4 bytes).
static const uint ALIVE_IN  = 8u;
static const uint ALIVE_OUT = 12u;

// Arguments
static const uint X_EMIT                 = 0u;
static const uint Y_EMIT                 = 4u;
static const uint Z_EMIT                 = 8u;
static const uint X_SIM                  = 12u;
static const uint Y_SIM                  = 16u;
static const uint Z_SIM                  = 20u;
static const uint VertexCountPerInstance = 24u;
static const uint InstanceCount          = 28u;
static const uint StartVertexLocation    = 32u;
static const uint StartInstanceLocation  = 36u;

struct Particle
{
  float3 pos;
  float3 vel;
  float  lifetime;
  float  _pad;
};

uint Hash(uint v)
{
  uint state = v * 747796405u + 2891336453u;
  uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}