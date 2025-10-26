#version 410 core
// Render a single GL_POINT at the light's world position as a billboarded orb.
uniform mat4 viewProj;   // projection * view
uniform vec3 lightPos;   // world-space
uniform float pointSize; // in pixels

void main() {
    gl_Position  = viewProj * vec4(lightPos, 1.0);
    gl_PointSize = pointSize; // needs GL_PROGRAM_POINT_SIZE enabled
}
