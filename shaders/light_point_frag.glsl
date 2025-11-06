#version 410 core
uniform vec3 lightColor;
out vec4 fragColor;

void main() {    
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;

    float alpha = smoothstep(1.0, 0.85, r2);
    fragColor = vec4(lightColor, alpha);
}