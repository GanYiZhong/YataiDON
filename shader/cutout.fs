#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Alpha-tested (cutout) material: the cabinet's _AT_ZERO_ / _AT_ONE_ materials. Their textures
// are binary alpha with black RGB under the transparent texels, so they must be discarded, not
// blended and never forced opaque.
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.5) discard;
    finalColor = vec4(texel.rgb, 1.0) * colDiffuse * fragColor;
}
