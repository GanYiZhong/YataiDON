#version 300 es
precision mediump float;

in vec2 fragTexCoord;
in float fragFacing;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 outlineParam;

void main() {
    // only the side that faces the camera draws a line; the model, drawn first, covers the rest
    if (fragFacing < -outlineParam.w) discard;
    float a = texture(texture0, fragTexCoord).a;
    if (a <= 0.0) discard;
    finalColor = vec4(0.05, 0.05, 0.05, a);
}
