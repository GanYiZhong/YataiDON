#version 300 es
precision mediump float;

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matView;
uniform mat4 matNormal;
uniform mat4 matModel;
uniform mat4 matProjection;
uniform vec4 outlineParam;   // x: thickness in pixels, y: base depth push, z: cap of the slope push (both in model units), w: 0 = front faces, 1 = back faces
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
    // Flat decals (face sheets) look straight at the camera: their n.xy is only noise, and
    // normalising it would move them a full thickness in a random direction, so the move
    // fades out below a few degrees of tilt.
    float tilt = length(n.xy);
    vec2 dir = (tilt > 0.0) ? n.xy / tilt : vec2(0.0);
    vec2 px = outlineParam.x * vertexColor.g * smoothstep(0.02, 0.15, tilt) * dir;
    vec2 ndc = px * 2.0 / screenSize;
    clip.xy += ndc * clip.w;
    // Depth: the line must stay behind the surface it belongs to, but in front of nothing it
    // should not be. The base push is in model units (scaled by the model matrix), because the
    // gaps it has to clear are part of the model: a face decal sits a few hundredths in front
    // of the drum head, and the feet sit a little further in front of the body. A vertex on a
    // receding slope also slides over surface that lies deeper than itself by lateral move *
    // slope, so that much is added, capped in model units so small parts keep their line.
    float model_scale = length(matModel[0].xyz);
    float thickness = outlineParam.x * 2.0 / screenSize.y / matProjection[1][1];   // view units
    float slope = tilt / max(abs(n.z), 0.2);
    float push = model_scale * outlineParam.y + min(1.5 * vertexColor.g * slope * thickness, model_scale * outlineParam.z);
    clip.z += abs(matProjection[2][2]) * push * clip.w;
    gl_Position = clip;
    fragTexCoord = vertexTexCoord;
    fragFacing = n.z;
}
