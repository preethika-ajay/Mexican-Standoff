#version 410 core

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in mat3 TBN;

uniform sampler2D colorMap;
uniform samplerCube environmentMap;
uniform sampler2D normalMap;

uniform bool hasTexCoords;
uniform bool hasNormalMap;
uniform bool useMaterial;
uniform bool enableEnvironmentMap;

uniform vec3 materialColor;
uniform vec3 viewPosition;

uniform float kd;
uniform float ks;
uniform float shininess;
uniform float reflectivity;

uniform int numLights;
uniform vec3 lightPosition[32];
uniform vec3 lightColor[32];
uniform float lightIntensity[32];

uniform int isGround; // ? FIX: add this so ground check works

out vec4 fragColor;

void main()
{
    vec3 albedo = hasTexCoords ? texture(colorMap, fragTexCoord).rgb : materialColor;
    vec3 N = normalize(fragNormal);

    // Apply normal map if enabled
    if (hasNormalMap) {
        vec3 normalSample = texture(normalMap, fragTexCoord).rgb;
        normalSample = normalSample * 2.0 - 1.0;
        N = normalize(TBN * normalSample);
    }

    // Handle ground separately (optional)
    if (isGround == 1) {
        fragColor = vec4(albedo * 0.8, 1.0);
        return;
    }

    vec3 V = normalize(viewPosition - fragPosition);
    vec3 rgb = vec3(0.0);

    float ambientStrength  = 0.08;
    float specularStrength = 0.25;

    for (int i = 0; i < numLights; ++i) {
        vec3 Ldir = lightPosition[i] - fragPosition;
        float dist = length(Ldir);
        vec3 L = (dist > 0.0) ? normalize(Ldir) : vec3(0.0);

        float NdotL = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);

        float att = 1.0 / (1.0 + dist * dist);

        vec3 ambient  = ambientStrength * albedo;
        vec3 diffuse  = kd * NdotL * albedo * lightColor[i];
        vec3 specular = ks * pow(NdotH, shininess) * lightColor[i];

        rgb += (ambient + diffuse + specular) * lightIntensity[i] * att;
    }

    // ? Environment reflection
    if (enableEnvironmentMap) {
        vec3 I = normalize(fragPosition - viewPosition);
        vec3 R = reflect(I, N);
        vec3 envColor = texture(environmentMap, R).rgb;
        rgb = mix(rgb, envColor, reflectivity);
    }

    rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2)); // gamma correction
    fragColor = vec4(rgb, 1.0);
}
