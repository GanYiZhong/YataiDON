#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform float outlineThickness;

out vec2 fragTexCoord;

void main() {
    // CPU skinning scales the normals with the bones (the arms squash to 0.9 / stretch to 2.4
    // in the cabinet's animations); normalize so the hull keeps one thickness.
    vec3 extruded = vertexPosition + normalize(vertexNormal) * outlineThickness;
    gl_Position = mvp * vec4(extruded, 1.0);
    fragTexCoord = vertexTexCoord;
}
