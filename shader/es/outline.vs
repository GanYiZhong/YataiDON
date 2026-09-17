#version 300 es
precision mediump float;

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matView;
uniform mat4 matNormal;
uniform vec4 outlineParam;   // x: thickness in pixels, y: depth push (clip z), z: unused, w: facing threshold
uniform vec2 screenSize;

out vec2 fragTexCoord;
out float fragFacing;

// Black line: each vertex moves outward in SCREEN space along its view-space normal, by
// thickness * vertexColor.g pixels (the green channel is the per-vertex thickness the models
// carry), and a little away from the camera so the model itself always wins the depth test.
void main() {
    vec4 clip = mvp * vec4(vertexPosition, 1.0);
    vec3 n = normalize(mat3(matView) * mat3(matNormal) * vertexNormal);
    vec2 dir = (dot(n.xy, n.xy) > 0.0) ? normalize(n.xy) : vec2(0.0);
    vec2 px = outlineParam.x * vertexColor.g * dir;
    clip.xy += px * 2.0 / screenSize * clip.w;
    clip.z += outlineParam.y * clip.w;
    gl_Position = clip;
    fragTexCoord = vertexTexCoord;
    fragFacing = n.z;
}
