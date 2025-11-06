#version 410 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

uniform vec3 viewPosition;
uniform int numLights;
uniform vec3 lightPosition[32];
uniform vec3 lightColor[32];
uniform float lightIntensity[32];

uniform float kd;
uniform float ks;


uniform float metallic;
uniform float roughness;
uniform vec3  albedo;

const float PI = 3.14159265359;


vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
void main()
{
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPosition - WorldPos);

    vec3 albedoColor = albedo;
    float metallicVal = metallic;
    float roughVal = roughness;

    vec3 F0 = mix(vec3(0.04), albedoColor, metallicVal);
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < numLights; ++i)
    {
        vec3 L = normalize(lightPosition[i] - WorldPos);
        vec3 H = normalize(V + L);
        float distance = length(lightPosition[i] - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColor[i] * lightIntensity[i] * attenuation;
       
        float NDF = 1.0 / (3.14159 * roughVal * roughVal + 1e-4);
        float G = 0.25;
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
        vec3 specPBR = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001);
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallicVal;

        float NdotL = max(dot(N, L), 0.0);
        vec3 pbrColor = (kD * albedoColor / 3.14159 + specPBR) * radiance * NdotL;

        vec3 R = reflect(-L, N);
        float diff = max(dot(N, L), 0.0);
        float spec = pow(max(dot(R, V), 0.0), 32.0);
        vec3 phongColor = (kd * albedoColor * diff + ks * spec * vec3(1.0)) * radiance;
 
        Lo += mix(phongColor, pbrColor, 0.5);
    }

    vec3 ambient = vec3(0.03) * albedoColor;
    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}

