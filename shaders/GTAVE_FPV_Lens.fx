// GTA V Enhanced FPV: the FPV camera's fisheye lens, the video look of the
// link (analog snow and torn lines, or digital freezes and macroblocks), and
// an optional goggles bezel around the picture.
//
// The GTAVE.FPV add-on sets the uniforms below every frame. With Active = 0
// the image passes through unchanged, so the technique stays enabled and runs
// after the game's other effects. The lens follows libfpv's model
// (docs/INTEGRATION.md#lens-distortion in libfpv): each output point samples
// at c * (1 + k1 r^2 + k2 r^4), with |c| = 1 at the corners. It is normalised
// so the picture's corners always sample the rendered image's corners: the
// frame stays fixed at any FOV and only the content bends.
#include "ReShade.fxh"

uniform float Active < hidden = true; > = 0.0;
uniform float LensK1 < hidden = true; > = 0.0;
uniform float LensK2 < hidden = true; > = 0.0;
uniform float Noise < hidden = true; > = 0.0;    // 0 perfect .. 1 unwatchable
uniform float Breakup < hidden = true; > = 0.0;  // chance of a torn or frozen frame
uniform float Digital < hidden = true; > = 0.0;  // 1: freezes and blocks instead of snow
// The goggles' screen inside the window, 0..1. The whole window without a bezel.
uniform float2 RectMin < hidden = true; > = float2(0.0, 0.0);
uniform float2 RectMax < hidden = true; > = float2(1.0, 1.0);
uniform float Bezel < hidden = true; > = 0.0;
uniform int FrameCount < source = "framecount"; >;

texture FrozenTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler Frozen { Texture = FrozenTex; };

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

// Lens over the picture rectangle: local 0..1 in, 0..1 out (corners fixed).
float2 lens_uv(float2 local, float aspect) {
    float d = 0.5 * sqrt(aspect * aspect + 1.0);
    float2 s = float2(aspect / d, 1.0 / d);
    float2 c = (local - 0.5) * s;
    float r2 = dot(c, c);
    float m = (1.0 + LensK1 * r2 + LensK2 * r2 * r2) / max(1.0 + LensK1 + LensK2, 0.05);
    return c * m / s + 0.5;
}

// 1 inside the goggles' rounded screen, 0 outside, with a soft edge.
float screen_mask(float2 uv) {
    if (Bezel < 0.5) return 1.0;
    float2 p = uv * BUFFER_SCREEN_SIZE;
    float2 lo = RectMin * BUFFER_SCREEN_SIZE, hi = RectMax * BUFFER_SCREEN_SIZE;
    float r = 0.045 * BUFFER_HEIGHT;
    float2 q = abs(p - (lo + hi) * 0.5) - ((hi - lo) * 0.5 - r);
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    return 1.0 - smoothstep(-1.5, 1.5, dist);
}

// A digital link holds its last good frame when it stutters.
bool frozen() { return Digital > 0.5 && hash(float2(floor(FrameCount / 3.0), 7.0)) < Breakup; }

float3 picture(float2 uv) {
    float2 size = RectMax - RectMin;
    float2 local = (uv - RectMin) / size;
    float aspect = size.x * BUFFER_WIDTH / (size.y * BUFFER_HEIGHT);
    float2 suv = lens_uv(local, aspect);

    float row = floor(uv.y * BUFFER_HEIGHT);
    float frame = FrameCount;
    if (Digital > 0.5) {
        // Macroblocks that grow with the noise.
        if (Noise > 0.2) {
            float blocks = lerp(180.0, 18.0, saturate((Noise - 0.2) * 1.5));
            float2 grid = float2(blocks * aspect, blocks);
            suv = (floor(suv * grid) + 0.5) / grid;
        }
    } else {
        // Torn bands of lines shift sideways.
        float tear = step(1.0 - Breakup * 0.5, hash(float2(floor(row / 6.0), frame)));
        suv.x += tear * (hash(float2(row, frame * 0.37)) - 0.5) * 0.04;
    }
    // A narrower screen (4:3 goggles on a 16:9 window) shows the centre crop.
    float crop = aspect / (BUFFER_WIDTH * BUFFER_RCP_HEIGHT);
    float2 scene = float2(0.5 + (suv.x - 0.5) * crop, suv.y);
    float3 col = tex2D(ReShade::BackBuffer, saturate(scene)).rgb;

    if (Digital < 0.5 && Noise > 0.0) {
        // Grey snow with rolling brighter and darker bands.
        float snow = hash(uv * BUFFER_SCREEN_SIZE + frame);
        float grey = snow * (0.75 + 0.25 * sin(uv.y * 40.0 + frame * 0.07));
        col = lerp(col, float3(grey, grey, grey), saturate(Noise * 0.8));
    }
    return col;
}

float4 PS_Lens(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    if (Active < 0.5) return tex2D(ReShade::BackBuffer, uv);
    if (frozen()) return float4(tex2D(Frozen, uv).rgb, 1.0);

    float mask = screen_mask(uv);
    float3 col = mask > 0.0 ? picture(uv) : float3(0.0, 0.0, 0.0);
    if (Bezel > 0.5) {
        // The goggles' dark interior, a little lighter towards the screen.
        float2 c = uv - (RectMin + RectMax) * 0.5;
        float3 shell = float3(0.012, 0.012, 0.014) * (1.6 - length(c * float2(1.0, 1.3)));
        col = lerp(shell, col, mask);
    }
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
