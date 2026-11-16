#pragma once

#include "ParticleSystem.hlsli"

RWStructuredBuffer<Particle> Particles : register(u0, space0);
RWStructuredBuffer<uint>     DeadList  : register(u0, space1); // LIFO.
RWByteAddressBuffer          Counters  : register(u0, space2);