// GTA V Enhanced FPV: the FPV camera's fisheye lens and the video look of the
// link (analog snow and torn lines, or digital freezes and macroblocks).
//
// The GTAVE.FPV add-on sets the uniforms below every frame. With Active = 0
// the image passes through unchanged, so the technique stays enabled and runs
// after the game's other effects. The lens follows libfpv's model
// (docs/INTEGRATION.md#lens-distortion in libfpv): each output point samples
// the scene at c * (1 + k1 r^2 + k2 r^4) / zoom, with |c| = 1 at the corners.
#include "ReShade.fxh"

uniform float Active < hidden = true; > = 0.0;
uniform float LensK1 < hidden = true; > = 0.0;
uniform float LensK2 < hidden = true; > = 0.0;
uniform float LensZoom < hidden = true; > = 1.0;
// tan(wanted FOV / 2) / tan(rendered FOV / 2): GTA caps the camera at 130 degrees.
uniform float FovScale < hidden = true; > = 1.0;
uniform float Noise < hidden = true; > = 0.0;    // 0 perfect .. 1 unwatchable
uniform float Breakup < hidden = true; > = 0.0;  // chance of a torn or frozen frame
uniform float Digital < hidden = true; > = 0.0;  // 1: freezes and blocks instead of snow
uniform int FrameCount < source = "framecount"; >;

texture FrozenTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler Frozen { Texture = FrozenTex; };

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float2 lens_uv(float2 uv) {
    float a = BUFFER_WIDTH * BUFFER_RCP_HEIGHT;
    float d = 0.5 * sqrt(a * a + 1.0);
    float2 s = float2(a / d, 1.0 / d);
    float2 c = (uv - 0.5) * s;
    float r2 = dot(c, c);
    float m = (1.0 + LensK1 * r2 + LensK2 * r2 * r2) / max(LensZoom, 0.001) * FovScale;
    return c * m / s + 0.5;
}

// A digital link holds its last good frame when it stutters.
bool frozen() { return Digital > 0.5 && hash(float2(floor(FrameCount / 3.0), 7.0)) < Breakup; }

float4 PS_Lens(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    if (Active < 0.5) return tex2D(ReShade::BackBuffer, uv);
    if (frozen()) return float4(tex2D(Frozen, uv).rgb, 1.0);

    float2 suv = lens_uv(uv);
    float row = floor(uv.y * BUFFER_HEIGHT);
    if (Digital > 0.5) {
        // Macroblocks that grow with the noise.
        if (Noise > 0.2) {
            float blocks = lerp(180.0, 18.0, saturate((Noise - 0.2) * 1.5));
            float2 grid = float2(blocks * BUFFER_WIDTH * BUFFER_RCP_HEIGHT, blocks);
            suv = (floor(suv * grid) + 0.5) / grid;
        }
    } else {
        // Torn bands of lines shift sideways.
        float frame = FrameCount;
        float tear = step(1.0 - Breakup * 0.5, hash(float2(floor(row / 6.0), frame)));
        suv.x += tear * (hash(float2(row, frame * 0.37)) - 0.5) * 0.04;
    }

    float3 col = tex2D(ReShade::BackBuffer, saturate(suv)).rgb;
    if (Digital < 0.5 && Noise > 0.0) {
        // Grey snow with rolling brighter and darker bands.
        float snow = hash(uv * BUFFER_SCREEN_SIZE + FrameCount);
        float grey = snow * (0.75 + 0.25 * sin(uv.y * 40.0 + FrameCount * 0.07));
        col = lerp(col, float3(grey, grey, grey), saturate(Noise * 0.8));
    }
    if (any(suv < 0.0) || any(suv > 1.0)) col = float3(0.0, 0.0, 0.0);  // outside the rendered image
    return float4(col, 1.0);
}

float4 PS_Store(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    return tex2D(ReShade::BackBuffer, uv);
}

technique GTAVE_FPV_Lens < ui_label = "GTAVE.FPV lens"; ui_tooltip = "Driven by the GTAVE.FPV add-on. Keep it enabled and last."; >
{
    pass Lens { VertexShader = PostProcessVS; PixelShader = PS_Lens; }
    pass Store { VertexShader = PostProcessVS; PixelShader = PS_Store; RenderTarget = FrozenTex; }
}
