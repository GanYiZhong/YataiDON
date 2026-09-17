#version 300 es
precision mediump float;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.5) discard;
    finalColor = vec4(texel.rgb, 1.0) * colDiffuse * fragColor;
}
