struct VS_OUTPUT
{
  float4 pos : SV_POSITION;
};

Texture2D<float4> HDRTexture : register(t0);

cbuffer TonemapConstants : register(b0)
{
  float exposure;
  float paper_white_nits;
  float peak_nits;
  uint  output_mode;
};

// See: https://30fps.net/pages/twotris/.
static const float2 FullscreenTriangle[3] = 
{
  float2(-1.f, -1.f),
  float2(-1.f, 3.f),
  float2(3.f, -1.f)
};

VS_OUTPUT VSMain(uint vertex_id : SV_VertexID)
{
  VS_OUTPUT output;

  output.pos = float4(FullscreenTriangle[vertex_id], 0.f, 1.f);
  return output;
}

// Rec.709 -> Rec2020 (BT.2087).
float3 ConvertRec709ToRec2020(float3 rgb)
{
  static const float3x3 BT2087 =
  {
    0.6274040f, 0.3292820f, 0.0433136f,
    0.0690970f, 0.9195400f, 0.0113612f,
    0.0163916f, 0.0880132f, 0.8955950f
  };

  return mul(BT2087, rgb);
}

float3 TonemapSDR(float3 hdr)
{
  hdr *= exp2(exposure);
  return hdr / (1.f + hdr);
}

float3 TonemapHdr(float3 hdr)
{
  // Extended Reinhard.
  /*{
    hdr *= exp2(exposure);

    float white  = peak_nits / paper_white_nits;
    float white2 = white * white;

    return (hdr * (1.f + hdr / white2)) / (1.f + hdr);
  }*/

  // Reinhard with an asymptote on peak/paper.
  {
    hdr *= exp2(exposure);
    float white  = peak_nits / paper_white_nits;

    return (hdr * white) / (white + hdr);
  }
}

// PQ-curve (SMPTE ST 2084).
float3 PQEncode(float3 nits)
{
  const float m1 = 2610.f / 16384.f;
  const float m2 = 2523.f / 32.f;
  const float c1 = 3424.f / 4096.f;
  const float c2 = 2413.f / 128.f;
  const float c3 = 2392.f / 128.f;

  float3 L = saturate(nits / 10000.f); // PQ encodes the range of 0–10000 nits.
  float3 Lm1 = pow(L, m1);

  float3 numerator   = c1 + c2 * Lm1;
  float3 denominator = 1.f + c3 * Lm1;

  return pow(numerator / denominator, m2);
}

float4 PSMain(VS_OUTPUT input) : SV_Target0
{
  int2 pixel = int2(input.pos.xy);
  float3 hdr = HDRTexture.Load(int3(pixel, 0)).rgb;

  switch(output_mode)
  {
    case 0: // SDR
    {
      float3 sdr = TonemapSDR(hdr);
      return float4(sdr, 1.f);
    }

    case 1: // HDR
    {
      float3 mapped  = TonemapHdr(hdr);
      float3 rec2020 = ConvertRec709ToRec2020(mapped);
      float3 nits    = rec2020 * paper_white_nits;
      float3 pq      = PQEncode(nits);

      return float4(pq, 1.f);
    }

    case 2: // scRGB
    {
      float3 mapped = TonemapHdr(hdr);
      float3 nits   = mapped * paper_white_nits;
      float3 scRGB  = nits / 80.f; // 1.f = 80 nits.

      return float4(scRGB, 1.f);
    }

    default:
      break;
  }

  return float4(0.f, 0.f, 0.f, 1.f);
}