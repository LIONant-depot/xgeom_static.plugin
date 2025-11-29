// LionMapper, co developed by Grok, is a real - time HDR - to - LDR tone mapping operator designed for photorealistic 
// rendering in engines like LIONAnt Engine, blending Uchimura's parametric piecewise curve for smooth shadow toe, linear mid-tones, 
// and exponential shoulder rolloff with Khronos-inspired offset for shadow fidelity and luminance-based scaling to prevent highlight 
// bleeding, hue shifts, or desaturation—ensuring pure colors like [10,0,0] map to [1,0,0] without spill. It compresses dynamic range 
// while preserving PBR accuracy and vibrancy, tunable via the P parameter (default 1.0 for SDR displays; increase to 1.5-2.0 for 
// HDR scenes to allow brighter peaks without clipping). To use it, integrate the GLSL function into your fragment shader post-PBR 
// lighting: call lionMapper(FinalColor.rgb, P) before gamma correction, where FinalColor is your HDR input; adjust P as a uniform 
// for scene-specific adaptation, test on high-contrast scenes to verify stability, and combine with auto-exposure for optimal 
// results in varying lighting conditions.
// 
// P: max brightness (1.0 default; tune 1-2 for HDR)
vec3 ToneMapper_lion(vec3 c, float P)
{
    float pk = max(c.r, max(c.g, c.b));
    if (pk <= 0.0) return c;

    // Khronos offset (handles negatives/lows)
    float mn = min(c.r, min(c.g, c.b));
    float off = (mn < 0.08) ? mn - 6.25 * mn * mn : 0.04;
    c -= off;
    pk = max(c.r, max(c.g, c.b));  // Recompute pk post-offset

    // Uchimura curve on pk (piecewise: toe/linear/shoulder)
    float a = 1.0, m = 0.22, l = 0.4, curve = 1.33, b = 0.0;
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float CP = -(a * P / (P - S1)) / P;
    float w0 = 1.0 - smoothstep(0.0, m, pk);
    float w2 = step(m + l0, pk);
    float w1 = 1.0 - w0 - w2;
    float T = m * pow(pk / m, curve) + b;
    float S = P - (P - S1) * exp(CP * (pk - S0));
    float L = m + a * (pk - m);
    float tm_pk = T * w0 + L * w1 + S * w2;

    // Scale color by ratio (no bleed/desat)
    return clamp(c * (tm_pk / pk), 0.0, 1.0);
}
