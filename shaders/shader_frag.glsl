#version 410 core

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform vec3 materialColor;   // from C++ when no texture

// Light uniforms (world space)
uniform vec3 lightPosition;
uniform vec3 lightColor;
uniform float lightIntensity;
uniform vec3 viewPosition;

layout(location = 0) out vec4 fragColor;

void main()
{
    // 1) Albedo (do NOT tint specular with this later)
    vec3 albedo =
    hasTexCoords ? texture(colorMap, fragTexCoord).rgb :
    (useMaterial ? materialColor : vec3(1.0));

    // 2) Geometry
    vec3  N = normalize(fragNormal);
    vec3  L = normalize(lightPosition - fragPosition);
    vec3  V = normalize(viewPosition  - fragPosition);
    vec3  H = normalize(L + V);

    // 3) Lighting params
    float ambientStrength  = 0.08;   // a touch more ambient
    float specularStrength = 0.5;
    float shininess        = 32.0;

    // 4) Distance attenuation (simple quadratic falloff)
    float dist = length(lightPosition - fragPosition);
    float att  = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);

    // 5) BRDF lobes
    float NdotL   = max(dot(N, L), 0.0);
    float NdotH   = max(dot(N, H), 0.0);

    vec3 ambient  = ambientStrength * albedo;
    vec3 diffuse  = NdotL * albedo * lightColor;
    // IMPORTANT: specular is NOT tinted by albedo for non-metals
    vec3 specular = specularStrength * pow(NdotH, shininess) * lightColor;

    // 6) Combine
    vec3 rgb = ((ambient + diffuse) * lightIntensity * att) + (specular * lightIntensity * att);

    // 7) Gamma correction (assume rendering to a non-sRGB FB)
    // If you enable GL_FRAMEBUFFER_SRGB, you can skip this pow().
    rgb = pow(max(rgb, 0.0), vec3(1.0 / 2.2));

    fragColor = vec4(rgb, 1.0);
}
