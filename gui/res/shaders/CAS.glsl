// Contrast-adaptive sharpening for chiaki-ng.
// Based on the public algorithm of AMD FidelityFX CAS (MIT license),
// rewritten as an mpv/libplacebo user shader. Runs on the final output image.
// @STRENGTH@ is replaced at load time (0.0 - 1.0).

//!DESC Contrast adaptive sharpening
//!HOOK OUTPUT
//!BIND HOOKED

#define CAS_SHARPNESS (@STRENGTH@)

vec4 hook()
{
    vec4 center = HOOKED_texOff(vec2(0.0, 0.0));

    vec3 a = HOOKED_texOff(vec2(-1.0, -1.0)).rgb;
    vec3 b = HOOKED_texOff(vec2( 0.0, -1.0)).rgb;
    vec3 c = HOOKED_texOff(vec2( 1.0, -1.0)).rgb;
    vec3 d = HOOKED_texOff(vec2(-1.0,  0.0)).rgb;
    vec3 e = center.rgb;
    vec3 f = HOOKED_texOff(vec2( 1.0,  0.0)).rgb;
    vec3 g = HOOKED_texOff(vec2(-1.0,  1.0)).rgb;
    vec3 h = HOOKED_texOff(vec2( 0.0,  1.0)).rgb;
    vec3 i = HOOKED_texOff(vec2( 1.0,  1.0)).rgb;

    // Local minimum / maximum of the plus-shaped neighbourhood, widened by the corners.
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    vec3 mn2 = min(mn, min(min(a, c), min(g, i)));
    mn += mn2;

    vec3 mx = max(max(max(d, e), max(f, b)), h);
    vec3 mx2 = max(mx, max(max(a, c), max(g, i)));
    mx += mx2;

    // Amount of sharpening adapts to local contrast: flat areas and
    // already-strong edges are sharpened less, which avoids halos and noise boost.
    vec3 rcpM = 1.0 / max(mx, vec3(1e-4));
    vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) * rcpM, 0.0, 1.0));

    float peak = -1.0 / mix(8.0, 5.0, clamp(CAS_SHARPNESS, 0.0, 1.0));
    vec3 w = amp * peak;
    vec3 rcpW = 1.0 / (1.0 + 4.0 * w);

    vec3 window = (b + d) + (f + h);
    vec3 outColor = clamp((window * w + e) * rcpW, 0.0, 1.0);

    return vec4(outColor, center.a);
}
