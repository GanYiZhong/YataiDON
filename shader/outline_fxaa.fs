#version 330

in vec2 fragTexCoord;
out vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 texSize;
uniform float outlineThickness;

#define FXAA_SPAN_MAX   8.0
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_REDUCE_MIN (1.0 / 128.0)

void main() {
    vec2 inv = 1.0 / texSize;
    vec2 uv = fragTexCoord;

    // FXAA operates on the raw rendered mesh first, so silhouette edges are
    // smoothed before the outline fill below reads their alpha.
    vec3 nw = texture(texture0, uv + vec2(-1.0, -1.0) * inv).rgb;
    vec3 ne = texture(texture0, uv + vec2(+1.0, -1.0) * inv).rgb;
    vec3 sw = texture(texture0, uv + vec2(-1.0, +1.0) * inv).rgb;
    vec3 se = texture(texture0, uv + vec2(+1.0, +1.0) * inv).rgb;
    vec4 m = texture(texture0, uv);

    const vec3 luma = vec3(0.299, 0.587, 0.114);
    float lNW = dot(nw, luma), lNE = dot(ne, luma);
    float lSW = dot(sw, luma), lSE = dot(se, luma);
    float lM = dot(m.rgb, luma);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec2 dir = vec2(
            -((lNW + lNE) - (lSW + lSE)),
            ((lNW + lSW) - (lNE + lSE))
        );

    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * inv;

    vec4 A = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)) +
                texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)));
    vec4 B = A * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5) +
                    texture(texture0, uv + dir * 0.5));

    float lB = dot(B.rgb, luma);
    vec4 aa = (lB < lMin || lB > lMax) ? A : B;
    aa.a = m.a;

    // Outline fill: solid pixels pass the anti-aliased color through;
    // background pixels within outlineThickness of the silhouette get the
    // dark halo, exactly as the old two-pass version did on raw alpha.
    if (aa.a > 0.001) {
        fragColor = vec4(aa.rgb, 1.0);
        return;
    }

    vec2 kern = inv * outlineThickness;
    float accum = 0.0;
    accum += texture(texture0, uv + vec2(-1.0,  0.0) * kern).a;
    accum += texture(texture0, uv + vec2( 1.0,  0.0) * kern).a;
    accum += texture(texture0, uv + vec2( 0.0, -1.0) * kern).a;
    accum += texture(texture0, uv + vec2( 0.0,  1.0) * kern).a;
    accum += texture(texture0, uv + vec2(-1.0, -1.0) * kern).a;
    accum += texture(texture0, uv + vec2( 1.0, -1.0) * kern).a;
    accum += texture(texture0, uv + vec2(-1.0,  1.0) * kern).a;
    accum += texture(texture0, uv + vec2( 1.0,  1.0) * kern).a;

    if (accum <= 0.0) {
        discard;
    }

    float alpha = clamp(accum / 2.0, 0.0, 1.0);
    fragColor = vec4(0.05, 0.05, 0.05, alpha);
}
