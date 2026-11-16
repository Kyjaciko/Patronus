VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
  VSOutput output;

  // DrawInstanced(4, kParticleCount, 0, 0).
  Particle p = gParticles[instanceID];
  float2 corner = QuadCorners[vertexID];

  // DrawIndexedInstanced(6 * kParticleCount, 1, 0, 0, 0).
  //Particle p = gParticles[vertexID / 4];
  //float2 corner = QuadCorners[vertexID % 4];

  float3 worldPos = p.pos
                    + gCamRight * corner.x * gBillboardSize
                    + gCamUp    * corner.y * gBillboardSize;

  output.pos = mul(float4(worldPos, 1.f), gViewProj);

  //float alpha = 1.f;
  float alpha = p.lifetime ? 1.f : 0.f;
  //output.color = float4(1.f * alpha, saturate(p.lifetime / 10.f) * alpha, 0.5f * alpha, alpha); // 10.f is starting value lifetime.
  //output.color = float4(0.f, alpha * length(p.pos) / 20.f, alpha * length(p.pos) / 10.f, alpha);
  output.color = float4(Hash(length(p.pos) / 10.f) * alpha, Hash(length(p.pos)) * alpha, Hash(length(p.pos) / 20.f) * alpha, alpha);
  output.corner = corner;

  return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
  float scene_z    = LinearizeDepth(gDepthBuffer.Load(int3(input.pos.xy, 0u)));
  float particle_z = input.pos.w;

  float distance_to_center           = length(input.corner);
  float circle_radius                = 1.f;
  float smoothstep_transition_region = .1f; // Artistic choice, subtracted from circle radius. Here transition region starts from .9f from the center.

  float smoothstep_transition_region_corrected = smoothstep_transition_region >= fwidth(distance_to_center) ? smoothstep_transition_region : fwidth(distance_to_center); // Artistic choice + anti-aliasing.

  float transition_pixels                      = 1.f; // Transition region is 1 pixel(s) wide, minimum is 1 pixel.
  float smoothstep_transition_region_in_pixels = transition_pixels * fwidth(distance_to_center); // Anti-aliasing.

  float within_circle = 1.f;
  //within_circle       = 1.f - step(circle_radius, distance_to_center); // Stepped transition (aliasing).
  //within_circle       = 1.f - smoothstep(circle_radius - smoothstep_transition_region, circle_radius, distance_to_center); // Smoothstep transition (no guaranteed anti-aliasing!).
  within_circle       = 1.f - smoothstep(circle_radius - smoothstep_transition_region_corrected, circle_radius, distance_to_center);
  //within_circle       = 1.f - smoothstep(circle_radius - smoothstep_transition_region_in_pixels, circle_radius, distance_to_center);

  float fade = saturate((scene_z - particle_z) / gFadeDistance);
  return input.color * fade * within_circle;
}