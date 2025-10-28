#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

uniform mat4 mvpMatrix;
uniform mat4 modelMatrix;
uniform mat3 normalModelMatrix;

out vec3 WorldPos;
out vec3 Normal;
out vec2 TexCoords;

void main()
{
    WorldPos = vec3(modelMatrix * vec4(aPos, 1.0));
    Normal   = normalize(normalModelMatrix * aNormal);
    TexCoords = aTexCoords;
    gl_Position = mvpMatrix * vec4(aPos, 1.0);
}
    