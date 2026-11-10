Texture2D<float4>   HDRTexture    : register(t0);
RWTexture2D<float4> BloomTexture  : register(u0);
SamplerState        LinearSampler : register(s0);

cbuffer BloomConstants : register(b0)
{
  float2 src_texel_size;
  uint2  dst_size;
  float  threshold;
  float  knee;
  float  upsample_scale;
  float  intensity;
  uint   flags;
};

static const float3 kLumaRec709   = float3(.2126f, .7152f, .0722f);
static const float  kMaxR11G11B10 = 65000.f; // Maximum value for R11G11B10 format.

static const uint   BLOOM_FLAG_ENABLE     = 1u << 0u;
static const uint   BLOOM_FLAG_FIRST_PASS = 1u << 1u;

float Luma(const float3 color)
{
  return dot(color, kLumaRec709);
}

float3 Tap(const float2 uv)
{
  return HDRTexture.SampleLevel(LinearSampler, uv, 0.f).rgb;
}

float4 KarisBlock(const float3 block, float kernel_weight)
{
  const float block_weight = kernel_weight / (1.f + Luma(block));
  return float4(block * block_weight, block_weight);
}

float3 SoftKneeThreshold(const float3 color, const float threshold, const float knee, const float epsilon = 1e-5f)
{
  if (threshold <= 0.f)
    return color;

  const float brightness   = Luma(color);
  float       soft         = clamp(brightness - threshold + knee, 0.f, 2.f * knee);
  soft                     = soft * soft / (4.f * knee + epsilon);
  const float contribution = max(soft, brightness - threshold) / max(brightness, epsilon);
  return color * contribution;
}

[numthreads(8, 8, 1)]
void DownSample(uint3 id : SV_DispatchThreadID)
{
  // Assure we stay within the bounds of the destination texture.
  if (any(id.xy >= dst_size))
    return;

  // Check if this is the first pass of the downsample (down0).
  const bool first_pass = (flags & BLOOM_FLAG_FIRST_PASS) != 0u;

  // UV-coordinate of the intersection between the 4 source texels.
  const float2 uv = (float2(id.xy) + .5f) / float2(dst_size);

  // Sample a 6x6 neighborhood of the source texture (each point has 4 texels around it in the source texture).
  //
  // a(-2,-2) .  b(0,-2) .  c(+2,-2)
  //    .   j(-1,-1) .  k(+1,-1) .
  // d(-2, 0) .  e(0, 0) .  f(+2, 0)
  //    .   l(-1,+1) .  m(+1,+1) .
  // g(-2,+2)  . h(0,+2) .  i(+2,+2)

  const float3 a = Tap(uv + src_texel_size * float2(-2.f, -2.f));
  const float3 b = Tap(uv + src_texel_size * float2( 0.f, -2.f));
  const float3 c = Tap(uv + src_texel_size * float2( 2.f, -2.f));
  const float3 d = Tap(uv + src_texel_size * float2(-2.f,  0.f));
  const float3 e = Tap(uv + src_texel_size * float2( 0.f,  0.f));
  const float3 f = Tap(uv + src_texel_size * float2( 2.f,  0.f));
  const float3 g = Tap(uv + src_texel_size * float2(-2.f,  2.f));
  const float3 h = Tap(uv + src_texel_size * float2( 0.f,  2.f));
  const float3 i = Tap(uv + src_texel_size * float2( 2.f,  2.f));
  const float3 j = Tap(uv + src_texel_size * float2(-1.f, -1.f));
  const float3 k = Tap(uv + src_texel_size * float2( 1.f, -1.f));
  const float3 l = Tap(uv + src_texel_size * float2(-1.f,  1.f));
  const float3 m = Tap(uv + src_texel_size * float2( 1.f,  1.f));

  const float3 blk_center = (j + k + l + m) * .25f;
  const float3 blk_tl     = (a + b + d + e) * .25f;
  const float3 blk_tr     = (b + c + e + f) * .25f;
  const float3 blk_bl     = (d + e + g + h) * .25f;
  const float3 blk_br     = (e + f + h + i) * .25f;

  float3 result;
  if (first_pass)
  {
    // Avoid fireflies (loose pixels) by using Karis-average.
    const float4 accumulator = KarisBlock(blk_center, .5f) + KarisBlock(blk_tl, .125f)
                             + KarisBlock(blk_tr, .125f)   + KarisBlock(blk_bl, .125f)
                             + KarisBlock(blk_br, .125f);

    result = accumulator.rgb / accumulator.a;
    result = SoftKneeThreshold(result, threshold, knee);
  }
  else
  {
    result = blk_center * .5f + (blk_tl + blk_tr + blk_bl + blk_br) * .125f;
  }

  BloomTexture[id.xy] = float4(result, 1.f);
}

[numthreads(8, 8, 1)]
void UpSample(uint3 id : SV_DispatchThreadID)
{
  // Assure we stay within the bounds of the destination texture.
  if (any(id.xy >= dst_size))
    return;

  // UV-coordinate of the middle of the destination texel.
  const float2 uv         = (float2(id.xy) + .5f) / float2(dst_size);
  const float2 texel_size = src_texel_size * upsample_scale;

  // Upsample using a tent filter.
  //
  // 1 2 1
  // 2 4 2   / 16
  // 1 2 1

  float3 tent = Tap(uv) * 4.f;

  tent += (Tap(uv + texel_size * float2( 0.f, -1.f)) +
           Tap(uv + texel_size * float2(-1.f,  0.f)) +
           Tap(uv + texel_size * float2( 1.f,  0.f)) +
           Tap(uv + texel_size * float2( 0.f,  1.f))) * 2.f;

  tent += (Tap(uv + texel_size * float2(-1.f, -1.f)) +
           Tap(uv + texel_size * float2( 1.f, -1.f)) +
           Tap(uv + texel_size * float2(-1.f,  1.f)) +
           Tap(uv + texel_size * float2( 1.f,  1.f)));

  tent *= 1.f / 16.f;

  // Read and write same texel, no race condition since each thread only touches id.xy.
  const float3 current = BloomTexture[id.xy].rgb;
  BloomTexture[id.xy]  = float4(min(current + tent, kMaxR11G11B10), 1.f); // Avoid going out of the colorspace bounds.
}