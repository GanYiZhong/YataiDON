#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matView;
uniform mat4 matNormal;
uniform mat4 matProjection;
uniform vec4 outlineParam;   // x: thickness in pixels, y: base depth push (clip z), z: unused, w: facing threshold
uniform vec2 screenSize;

out vec2 fragTexCoord;
out float fragFacing;

// Black line: each vertex moves outward in SCREEN space along its view-space normal, by
// thickness * vertexColor.g pixels (the green channel is the per-vertex thickness the models
// carry), and away from the camera so the model itself always wins the depth test.
// The camera is orthographic, so clip z is linear in view depth.
void main() {
    vec4 clip = mvp * vec4(vertexPosition, 1.0);
    vec3 n = normalize(mat3(matView) * mat3(matNormal) * vertexNormal);
    vec2 dir = (dot(n.xy, n.xy) > 0.0) ? normalize(n.xy) : vec2(0.0);
    vec2 px = outlineParam.x * vertexColor.g * dir;
    vec2 ndc = px * 2.0 / screenSize;
    clip.xy += ndc * clip.w;
    // A vertex on a receding slope slides over surface that lies deeper than the vertex itself
    // (by lateral move * slope), so push it back at least that far, plus a base margin.
    vec2 lateral = vec2(ndc.x / matProjection[0][0], ndc.y / matProjection[1][1]);
    float slope = length(n.xy) / max(abs(n.z), 0.2);
    float push = outlineParam.y + abs(matProjection[2][2]) * length(lateral) * slope * 1.5;
    clip.z += push * clip.w;
    gl_Position = clip;
    fragTexCoord = vertexTexCoord;
    fragFacing = n.z;
}
