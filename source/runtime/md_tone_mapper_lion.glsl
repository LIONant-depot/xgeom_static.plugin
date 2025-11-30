// LionMapper, is a real - time HDR - to - LDR tone mapping operator designed for photorealistic 
// rendering in engines like LIONAnt Engine.
// P: max brightness (1.0 default; tune 1-2 for HDR)

vec3 ToneMapper_lion(vec3 c, float p)
{  
    float P = 1;
    float pk = max(c.r, max(c.g, c.b));
    if (pk <= 0.0) return c;

    // Params for AgX approx + brighter, less contrast
    float a = 1.0, m = 0.0, l = 0.3, curve = 1.33, b = -0.001;
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

    c *= tm_pk / pk;  // Scale first

    // Desat highs (desat=0.15 unchanged)
    float desat = 0.15;
    float g = 1.0 - 1.0 / (desat * (pk - tm_pk) + 1.0);
    c = mix(c, vec3(tm_pk), g * smoothstep(0.76, 1.0, tm_pk));

    return clamp(c + 0.4 * c, 0.0, 1.0);
}