#version 300 es
precision mediump float;

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;

void main() {
    // The hull is drawn before the model and writes depth: a texel the material discards
    // (alpha-tested fins, tentacles, hair tips) must not leave a black wall behind it.
    if (texture(texture0, fragTexCoord).a < 0.5) discard;
    finalColor = vec4(0.05, 0.05, 0.05, 1.0);
}
