#version 410 core

uniform mat4 viewProj;   
uniform vec3 lightPos;   
uniform float pointSize; 

void main() {
    gl_Position  = viewProj * vec4(lightPos, 1.0);
    gl_PointSize = pointSize; 
}
