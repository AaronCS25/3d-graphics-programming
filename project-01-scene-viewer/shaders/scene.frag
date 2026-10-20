#version 330 core
// Blinn-Phong (task 09) with the shadow map of task 10, the maps and TBN of
// task 11 and the MTL materials of task 12. uViewMode swaps the final colour
// for one of the diagnostic views.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;
in vec4 vTangent;
in vec4 vLightSpacePos;
in float vDistance;
in float vViewDepth;

// Ambient light uses Kd: exporters write Ka 0 or Ka 1 regardless of the
// surface (task 12).
struct Material {
    vec3 Kd;
    vec3 Ks;
    float Ns;
    float d;
};

uniform Material uMaterial;
uniform sampler2D uDiffuseMap;  // map_Kd
uniform sampler2D uSpecularMap; // map_Ks
uniform sampler2D uNormalMap;   // map_Bump / norm

uniform sampler2D uShadowMap;
uniform bool uShadows;
uniform bool uPcf;
uniform float uBiasScale;
uniform vec3 uLightDir; // towards the light, unit length
uniform vec3 uCameraPos;
uniform vec3 uAmbientLight; // Ia
uniform vec3 uLightColor;   // Id = Is

uniform bool uFlatNormals;
uniform int uViewMode; // 0 lit, 1 albedo, 2 normals, 3 depth, 4 uv, 5 distance, 6 light
uniform int uColormap; // 0 jet, 1 plasma
uniform bool uIsolines;
uniform float uIsolineCount;
uniform float uDepthNear;
uniform float uDepthFar;

out vec4 FragColor;

// The colormap functions are copied verbatim from kbinani/colormap-shaders
// (MIT, Copyright (c) 2015 kbinani), as in task 06: MATLAB_jet and IDL_Plasma.
// Each maps x in [0, 1] to an RGB colour.
// --- MATLAB_jet (kbinani/colormap-shaders) ---------------------------------
float jet_red(float x) {
    if (x < 0.7) {
        return 4.0 * x - 1.5;
    } else {
        return -4.0 * x + 4.5;
    }
}

float jet_green(float x) {
    if (x < 0.5) {
        return 4.0 * x - 0.5;
    } else {
        return -4.0 * x + 3.5;
    }
}

float jet_blue(float x) {
    if (x < 0.3) {
       return 4.0 * x + 0.5;
    } else {
       return -4.0 * x + 2.5;
    }
}

vec4 jet(float x) {
    float r = clamp(jet_red(x), 0.0, 1.0);
    float g = clamp(jet_green(x), 0.0, 1.0);
    float b = clamp(jet_blue(x), 0.0, 1.0);
    return vec4(r, g, b, 1.0);
}

// --- IDL_Plasma (kbinani/colormap-shaders) ---------------------------------
float plasma_red(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 12.16378802377247;
    const float b = 0.05245257017955226;
    const float c = 0.2532139106569052;
    const float d = 0.02076964056039702;
    const float e = 270.124167081014;
    const float f = 1.724941960305955;
    float v = (a * x + b) * sin(2.0 * pi / c * (x - d)) + e * x + f;
    if (v > 255.0) {
        return 255.0 - (v - 255.0);
    } else {
        return v;
    }
}

float plasma_green(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 88.08537391182792;
    const float b = 0.25280516046667;
    const float c = 0.05956080245692388;
    const float d = 106.5684078925541;
    return a * sin(2.0 * pi / b * (x - c)) + d;
}

float plasma_blue(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 63.89922420106684;
    const float b = 0.4259605778503662;
    const float c = 0.2529247343450655;
    const float d = 0.5150868195804643;
    const float e = 938.1798072557968;
    const float f = 503.0883490697431;
    float v = (a * x + b) * sin(2.0 * pi / c * x + d * 2.0 * pi) - e * x + f;
    if (v > 255.0) {
        return 255.0 - (v - 255.0);
    } else {
        return mod(v, 255.0);
    }
}

vec4 plasma(float x) {
    float r = clamp(plasma_red(x) / 255.0, 0.0, 1.0);
    float g = clamp(plasma_green(x) / 255.0, 0.0, 1.0);
    float b = clamp(plasma_blue(x) / 255.0, 0.0, 1.0);
    return vec4(r, g, b, 1.0);
}
// ---------------------------------------------------------------------------

vec3 colormap(float x) {
    return (uColormap == 0 ? jet(x) : plasma(x)).rgb;
}

// Task 10's shadow test: bias counted in texels, optionally 3x3 PCF.
float shadowFactor(vec4 lightSpacePos, vec3 N, vec3 L) {
    vec3 p = lightSpacePos.xyz / lightSpacePos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) {
        return 0.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = uBiasScale * texel.x * max(10.0 * (1.0 - dot(N, L)), 1.0);

    if (!uPcf) {
        return p.z - bias > texture(uShadowMap, p.xy).r ? 1.0 : 0.0;
    }
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            shadow += p.z - bias > closest ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 V = normalize(uCameraPos - vWorldPos);

    // A scene file does not promise that every wall faces the viewer: seen
    // from behind, a face is lit on the camera's side. Flat normals come from
    // the screen-space derivatives of the position, one per triangle.
    vec3 geometric;
    if (uFlatNormals) {
        geometric = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
        if (dot(geometric, V) < 0.0) {
            geometric = -geometric;
        }
    } else {
        geometric = normalize(vNormal);
        if (!gl_FrontFacing) {
            geometric = -geometric;
        }
    }

    // Task 11's TBN; tangent.w flips B where the texture is mirrored.
    vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, geometric) * geometric);
    vec3 B = cross(geometric, T) * vTangent.w;
    vec3 mapped = texture(uNormalMap, vTexCoords).rgb * 2.0 - 1.0;
    vec3 N = normalize(mat3(T, B, geometric) * mapped);

    vec3 albedo = uMaterial.Kd * texture(uDiffuseMap, vTexCoords).rgb;
    vec3 ks = uMaterial.Ks * texture(uSpecularMap, vTexCoords).rgb;

    vec3 L = uLightDir;
    float shadow = uShadows ? shadowFactor(vLightSpacePos, geometric, L) : 0.0;
    float lambert = max(dot(N, L), 0.0);

    if (uViewMode == 1) {
        FragColor = vec4(albedo, 1.0);
        return;
    }
    if (uViewMode == 2) {
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    if (uViewMode == 3) {
        float depth = (vViewDepth - uDepthNear) / (uDepthFar - uDepthNear);
        FragColor = vec4(colormap(clamp(depth, 0.0, 1.0)), 1.0);
        return;
    }
    if (uViewMode == 4) {
        FragColor = vec4(fract(vTexCoords), 0.0, 1.0);
        return;
    }
    if (uViewMode == 5) {
        // Task 06: the distance map, shaded towards the camera so the shape
        // stays readable. Surfaces without a distance are grey.
        float shade = 0.35 + 0.65 * abs(dot(geometric, V));
        if (vDistance < 0.0) {
            FragColor = vec4(vec3(0.45) * shade, 1.0);
            return;
        }
        vec3 base = colormap(vDistance);
        // Isolines: the first 15% of each band is darkened. Every band edge is
        // a curve of equal geodesic distance, the front itself.
        if (uIsolines && fract(vDistance * uIsolineCount) < 0.15) {
            base *= 0.35;
        }
        FragColor = vec4(base * shade, 1.0);
        return;
    }
    if (uViewMode == 6) {
        // What the sun reaches: Lambert's term, black where it is in shadow.
        FragColor = vec4(vec3((1.0 - shadow) * lambert), 1.0);
        return;
    }

    vec3 ambient = albedo * uAmbientLight;
    vec3 diffuse = albedo * uLightColor * lambert;
    float highlight = 0.0;
    if (lambert > 0.0) {
        // Blinn-Phong with 4 Ns, which matches the Phong lobe of exponent Ns.
        vec3 H = normalize(L + V);
        highlight = pow(max(dot(N, H), 0.0), 4.0 * uMaterial.Ns);
    }
    vec3 specular = ks * uLightColor * highlight;

    FragColor = vec4(ambient + (1.0 - shadow) * (diffuse + specular), uMaterial.d);
}
