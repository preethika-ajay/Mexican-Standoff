#version 410 core
uniform vec3 pathColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(pathColor, 1.0);
}