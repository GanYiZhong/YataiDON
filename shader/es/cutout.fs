#version 300 es
precision mediump float;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Character main pass. The vertex colour is not a tint (its green channel is the outline
// thickness), so it is ignored. Only fully transparent texels are cut out; anything else is
// written with its own alpha and blended by the current blend mode.
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a <= 0.0) discard;
    finalColor = texel * colDiffuse;
}
