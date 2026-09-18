#version 330

in vec2 fragTexCoord;
in float fragFacing;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 outlineParam;

void main() {
    // Two passes: w = 0 keeps the camera-facing vertices, w = 1 the ones facing away (drawn
    // after the model's back faces went into the depth buffer, so only their overhang beyond
    // the silhouette shows).
    if (outlineParam.w > 0.5) { if (fragFacing >= 0.0) discard; }
    else if (fragFacing < 0.0) discard;
    float a = texture(texture0, fragTexCoord).a;
    if (a <= 0.0) discard;
    finalColor = vec4(0.05, 0.05, 0.05, a);
}
