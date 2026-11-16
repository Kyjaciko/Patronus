#pragma once

#include "ParticleSystem.hlsli"

static const float2 QuadCorners[4] = 
{
  float2(-1.f, 1.f),
  float2(1.f, 1.f),
  float2(-1.f, -1.f),
  float2(1.f, -1.f)
};

struct VSOutput
{
  float4 pos    : SV_POSITION;
  float4 color  : COLOR;
  float2 corner : TEXCOORD0;
};

StructuredBuffer<Particle> Particles     : register(t0, space0);
Texture2D<float>           DepthBuffer   : register(t0, space1);
StructuredBuffer<uint>     AliveList_OUT : register(t0, space2);
RWByteAddressBuffer        Counters      : register(u0, space0);

cbuffer CameraCB : register(b0)
{
  row_major float4x4 gViewProj;

  float3 gCamRight;
  float  gBillboardSize;

  float3 gCamUp;
  float  gPad1;

  float gCamNear;
  float gCamFar;
  float gFadeDistance;
  float gPad2;
};

// Reverse-Z depth!
float LinearizeDepth(float depth)
{
  return gCamNear * gCamFar / (depth * (gCamFar - gCamNear) + gCamNear);
}