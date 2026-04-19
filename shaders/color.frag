#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uSrc;

// every knob is a DELTA from identity. 0 = no change for everything. nice and uniform.
layout(push_constant) uniform Knobs {
    float brightness;
    float contrast;
    float exposure;
    float saturation;

    float vibrance;
    float hue_deg;
    float gamma;
    float temperature;

    float tint;
    float red_gain;
    float green_gain;
    float blue_gain;

    float shadows;
    float midtones;
    float highlights;
    float _pad;
} K;

vec3 rgb2hsv(vec3 c)
{
    vec4 k = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 k = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + k.xyz) * 6.0 - k.www);
    return c.z * mix(k.xxx, clamp(p - k.xxx, 0.0, 1.0), c.y);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec3 c = texture(uSrc, vUV).rgb;

    // exposure (linear scale)
    c *= exp2(K.exposure);

    // per-channel RGB gain, slider is delta around 1.0
    c *= vec3(1.0) + vec3(K.red_gain, K.green_gain, K.blue_gain);

    // temperature (warm = red+, cool = blue+) and tint (magenta+ / green-)
    c.r += K.temperature * 0.10;
    c.b -= K.temperature * 0.10;
    c.g -= K.tint        * 0.10;
    c.r += K.tint        * 0.05;
    c.b += K.tint        * 0.05;

    // contrast around mid-grey
    c = (c - 0.5) * (1.0 + K.contrast) + 0.5;

    // brightness (additive)
    c += K.brightness;

    // 3-band: shadows lift dark end, highlights lift bright end, midtones gain mid
    float l = clamp(luma(c), 0.0, 1.0);
    float shadow_w    = pow(1.0 - l, 2.0);
    float highlight_w = pow(l, 2.0);
    float mid_w       = 1.0 - shadow_w - highlight_w;
    c += K.shadows    * shadow_w;
    c += K.highlights * highlight_w;
    c *= 1.0 + K.midtones * mid_w;

    // saturation
    float gy = luma(c);
    c = mix(vec3(gy), c, 1.0 + K.saturation);

    // vibrance (smarter saturation, leaves already-saturated pixels alone)
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float sat  = maxC - minC;
    c = mix(vec3(luma(c)), c, 1.0 + K.vibrance * (1.0 - sat));

    // hue rotation
    if (abs(K.hue_deg) > 0.001)
    {
        vec3 hsv = rgb2hsv(max(c, vec3(0.0)));
        hsv.x = fract(hsv.x + K.hue_deg / 360.0);
        c = hsv2rgb(hsv);
    }

    // gamma. + = brighter mids, - = darker mids
    if (abs(K.gamma) > 0.001)
        c = pow(max(c, vec3(0.0)), vec3(1.0 / exp2(K.gamma)));

    oColor = vec4(c, 1.0);
}
