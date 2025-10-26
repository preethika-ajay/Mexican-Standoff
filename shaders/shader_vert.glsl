#version 410 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) in vec3 tangent;

uniform mat4 mvpMatrix;
uniform mat4 modelMatrix;
uniform mat3 normalModelMatrix;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out mat3 TBN;

void main()
{
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    fragPosition  = worldPos.xyz;
    fragTexCoord  = texCoord;

    vec3 N = normalize(normalModelMatrix * normal);
    vec3 T = normalize(normalModelMatrix * tangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);
    
    fragNormal = N;

    gl_Position = mvpMatrix * vec4(position, 1.0);
}