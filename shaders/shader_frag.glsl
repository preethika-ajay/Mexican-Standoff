#version 410 core

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in mat3 TBN;

uniform sampler2D  colorMap;
uniform sampler2D  normalMap;
uniform samplerCube environmentMap;

uniform bool hasTexCoords;
uniform bool hasNormalMap;
uniform bool useMaterial;
uniform bool enableEnvironmentMap;

uniform vec3 materialColor;
uniform vec3 viewPosition;

// material
uniform float kd;
uniform float ks;
uniform float shininess;
uniform float reflectivity;

// lights
uniform int   numLights;
uniform vec3  lightPosition[32];
uniform vec3  lightColor[32];
uniform float lightIntensity[32];

// shadows (ground receiver only)
const int MAX_SHADOW_LIGHTS = 16;
uniform int  numShadowMaps;
uniform sampler2D shadowMaps[MAX_SHADOW_LIGHTS];
uniform mat4 lightViewProj[MAX_SHADOW_LIGHTS];
uniform vec2 shadowTexelSize;     // (pcfScale/size, pcfScale/size)
uniform float shadowBias;
uniform float shadowMinNdotL;     // skip lights nearly parallel to ground

uniform int isGround;

out vec4 fragColor;

float shadowPCF(int idx, vec3 worldPos, float bias)
{
    vec4 clip = lightViewProj[idx] * vec4(worldPos, 1.0);
    vec3 proj = clip.xyz / max(clip.w, 1e-5);
    vec2 uv   = proj.xy * 0.5 + 0.5;
    float z   = proj.z * 0.5 + 0.5;

    // outside map => lit
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 0.0;

    float s = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        vec2 offs  = vec2(dx, dy) * shadowTexelSize;
        float depth = texture(shadowMaps[idx], uv + offs).r;
        s += (z - bias) > depth ? 1.0 : 0.0;
    }
    return s / 9.0;
}

void main()
{
    vec3 albedo = materialColor;   // no diffuse texture

    // -------- Ground: only ambient + shadows (no direct lighting) --------
    if (isGround == 1) {
        albedo = texture(colorMap, fragTexCoord).rgb;
        const float ambientGround  = 0.20;
        const float shadowStrength = 0.85;

        vec3 Nground = normalize(fragNormal);
        float avgShadow = 0.0; int valid = 0;
        int ns = min(numShadowMaps, MAX_SHADOW_LIGHTS);

        for (int i = 0; i < ns; ++i) {
            vec3 Ldir = normalize(lightPosition[i] - fragPosition);
            float ndotl_abs = abs(dot(Nground, Ldir)); // 1: perpendicular, 0: parallel
            if (ndotl_abs < shadowMinNdotL) continue;

            // slope-scaled bias: more at grazing angles
            float bias = mix(shadowBias * 0.30, shadowBias * 1.50, 1.0 - ndotl_abs);

            avgShadow += shadowPCF(i, fragPosition, bias);
            ++valid;
        }
        if (valid > 0) avgShadow /= float(valid);

        vec3 rgb = albedo * (ambientGround * (1.0 - shadowStrength * avgShadow));
        rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2));  // gamma
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // -------- Objects: Blinn-Phong with kd/ks (+ optional normal map) --------
    vec3 N = normalize(fragNormal);
    if (hasNormalMap) {
        vec3 nm = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * nm);
    }

    vec3 V = normalize(viewPosition - fragPosition);
    vec3 rgb = vec3(0.0);
    const float ambientStrength = 0.08;

    for (int i = 0; i < numLights; ++i) {
        vec3 Ldir = lightPosition[i] - fragPosition;
        float dist = length(Ldir);
        vec3  L = (dist > 0.0) ? (Ldir / dist) : vec3(0.0);
        float att = 1.0 / (1.0 + dist * dist);

        float NdotL = max(dot(N, L), 0.0);
        vec3  H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);

        vec3 ambient  = ambientStrength * albedo;
        vec3 diffuse  = kd * NdotL * albedo * lightColor[i];
        vec3 specular = ks * pow(NdotH, shininess) * lightColor[i];

        rgb += (ambient + diffuse + specular) * lightIntensity[i] * att;
    }

    // Optional environment reflections
    if (enableEnvironmentMap) {
        float refl = clamp(reflectivity, 0.0, 1.0);
        vec3 I = normalize(fragPosition - viewPosition); // = -V
        vec3 R = reflect(I, N);
        vec3 env = texture(environmentMap, R).rgb;
        rgb = mix(rgb, env, refl);
    }

    rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2));  // gamma
    fragColor = vec4(rgb, 1.0);
}
