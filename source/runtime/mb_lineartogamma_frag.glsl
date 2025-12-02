

// standard IEC 61966-2-1 sRGB encoding formula
vec3 linearToSrgb(vec3 linear) 
{
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, linear));
}

vec3 linearToSrgbVariable(vec3 linear, float gamma /*= 2.2*/) 
{
    if (gamma == 1.0) return linear;
    float phi = 1.0 / gamma;
    float a = 1.055;
    float offs = 0.055;
    float one_minus_phi = 1.0 - phi;
    float temp = offs / (a * one_minus_phi);
    float thresh = pow(temp, 1.0 / phi);
    float c = a * phi * (temp / thresh);
    return mix(c * linear, a * pow(linear, vec3(phi)) - offs, step(vec3(thresh), linear));
}