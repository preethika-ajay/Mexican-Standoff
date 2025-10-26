#version 410 core

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform vec3 materialColor;
uniform float shininess;

uniform float kd;   // diffuse coefficient (0..1+)
uniform float ks;   // specular coefficient (0..1+)

uniform vec3 viewPosition;

const int MAX_LIGHTS = 32;
uniform int  numLights;
uniform vec3 lightPosition[MAX_LIGHTS];
uniform vec3 lightColor   [MAX_LIGHTS];
uniform float lightIntensity[MAX_LIGHTS];

const int MAX_SHADOW_LIGHTS = 16;
uniform int  numShadowMaps;                         // <= numLights
uniform sampler2D shadowMaps[MAX_SHADOW_LIGHTS];    // bound to units 1..N
uniform mat4 lightViewProj[MAX_SHADOW_LIGHTS];
uniform vec2 shadowTexelSize;   // (1/size, 1/size) scaled
uniform float shadowBias;
uniform int  isGround;          // 1 when drawing ground plane

// NEW: if |dot(groundN, lightDir)| < shadowMinNdotL, skip this light's shadow on ground
uniform float shadowMinNdotL;   // 0.00..0.20 (parallel threshold)

out vec4 fragColor;

// --------- PCF shadow helper (3x3) ----------
float shadowPCF(int idx, vec3 worldPos, float bias)
{
    vec4 clip = lightViewProj[idx] * vec4(worldPos, 1.0);
    vec3 proj = clip.xyz / max(clip.w, 1e-5);
    vec2 uv   = proj.xy * 0.5 + 0.5;
    float z   = proj.z * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 0.0;

    float s = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        vec2 offs = vec2(dx, dy) * shadowTexelSize;
        float depth = texture(shadowMaps[idx], uv + offs).r;
        s += (z - bias) > depth ? 1.0 : 0.0;
    }
    return s / 9.0;
}

void main()
{
    // Base color (texture if available, otherwise uniform)
    vec3 albedo = hasTexCoords ? texture(colorMap, fragTexCoord).rgb : materialColor;

    // ---------- SPECIAL CASE: GROUND ----------
    // Ground is unaffected by lighting; only darkened by soft PCF shadows.
    if (isGround == 1) {
        const float ambientGround  = 0.20;
        const float shadowStrength = 0.85;

        vec3 groundN = normalize(fragNormal);

        float avgShadow = 0.0;
        int   ns = min(numShadowMaps, MAX_SHADOW_LIGHTS);
        int   valid = 0;

        if (ns > 0) {
            for (int i = 0; i < ns; ++i) {
                // Direction from ground point to light
                vec3 Ldir = normalize(lightPosition[i] - fragPosition);

                // Skip lights that are (nearly) parallel to the ground
                float ndotl_abs = abs(dot(groundN, Ldir)); // 1=perp, 0=parallel
                if (ndotl_abs < shadowMinNdotL)
                continue;

                // Slope-scaled bias: small when perpendicular, larger at grazing angles
                float bias = mix(shadowBias * 0.30,  // min bias (perpendicular)
                shadowBias * 1.50,  // max bias (grazing)
                1.0 - ndotl_abs);

                avgShadow += shadowPCF(i, fragPosition, bias);
                ++valid;
            }

            if (valid > 0) avgShadow /= float(valid);
        }

        vec3 rgb = albedo * (ambientGround * (1.0 - shadowStrength * avgShadow));
        rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2)); // gamma approx
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // ---------- REGULAR OBJECT SHADING (characters, etc.) ----------
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPosition - fragPosition);

    float ambientStrength  = 0.08;
    float specularStrength = 0.25;

    vec3 rgb = vec3(0.0);

    for (int i = 0; i < numLights; ++i) {
        vec3 Ldir = lightPosition[i] - fragPosition;
        float dist = length(Ldir);
        vec3  L = (dist > 0.0) ? Ldir / dist : vec3(0.0);

        float NdotL = max(dot(N, L), 0.0);

        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);

        float att = 1.0 / (1.0 + dist * dist);

        vec3 ambient  = ambientStrength * albedo;
        vec3 diffuse  = kd * NdotL * albedo * lightColor[i];
        vec3 specular = ks * pow(NdotH, shininess) * lightColor[i];


        rgb += (ambient + diffuse + specular) * lightIntensity[i] * att;
    }

    rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2));
    fragColor = vec4(rgb, 1.0);
}
