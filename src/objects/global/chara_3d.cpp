#include "chara_3d.h"
#include "../../libs/animation.h"
#include "../../libs/camera_utils.h"
#include "../../libs/global_data.h"
#include "../../libs/perf.h"   // ROUND 103: lazy render-target event recorder
#include "../../libs/scores.h"
#include <cstdlib>   // ROUND 103: getenv for YATAIDON_R103_DISABLE
#include <fstream>
#include <rapidjson/document.h>
namespace ray {
#include <raymath.h>
}
extern "C" { void rlSetCullFace(int mode); }
static constexpr int RL_CULL_FACE_FRONT = 0;
static constexpr int RL_CULL_FACE_BACK  = 1;
// TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - raylib exposes these two
// internal-matrix getters via rlgl.h; declared directly (matching this file's
// existing rlSetCullFace pattern) instead of #include-ing all of rlgl.h.
extern "C" { ray::Matrix rlGetMatrixModelview(void); ray::Matrix rlGetMatrixProjection(void); }
// TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - forces raylib's pending 2D
// batch to actually execute, so a raw glReadPixels right after sees what was
// just drawn instead of whatever happened to already be flushed.
extern "C" { void rlDrawRenderBatchActive(void); }

// TEMP ROUND33 DIAGNOSTIC (r33-chara3d-glstate) - dumps raw GL state right
// before draw_outline()/draw_3d() run, gated behind YATAIDON_R33_GLSTATE so it
// is a no-op for every normal build/run. Must be removed before finishing.
// See Graphics/game/MAPPING_hud.md ROUND 33 and ENGINE_BINDINGS.md ROUND 33.
#include <GL/gl.h>
// MinGW's GL/gl.h drags in <windows.h>, whose LoadImageA/W macro shadows
// ray::LoadImage used later in this file - undo the macro immediately.
#ifdef LoadImage
#undef LoadImage
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_BLEND_SRC_ALPHA
#define GL_BLEND_SRC_ALPHA 0x80CB
#endif
#ifndef GL_BLEND_DST_ALPHA
#define GL_BLEND_DST_ALPHA 0x80CA
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_ENABLED
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#endif
// glGetVertexAttribiv is GL 2.0+, not in MinGW's GL/gl.h (1.1) - resolve it at
// runtime via WGL instead of pulling in glad (which would collide with the
// implementation already linked from raylib itself).
typedef void (APIENTRY *R33_PFNGLGETVERTEXATTRIBIVPROC)(unsigned int, unsigned int, int*);
static R33_PFNGLGETVERTEXATTRIBIVPROC r33_glGetVertexAttribiv() {
    static PROC p = wglGetProcAddress("glGetVertexAttribiv");
    return (R33_PFNGLGETVERTEXATTRIBIVPROC)p;
}
static void r33_dump_gl_state(const char* label) {
    if (!getenv("YATAIDON_R33_GLSTATE")) return;
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depth_mask = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLboolean stencil_test = glIsEnabled(GL_STENCIL_TEST);
    GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
    GLint cull_face_mode = 0;
    glGetIntegerv(GL_CULL_FACE_MODE, &cull_face_mode);
    GLint front_face = 0;
    glGetIntegerv(GL_FRONT_FACE, &front_face);
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    GLint active_texture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    GLint tex_binding_2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex_binding_2d);
    GLint blend_src_rgb = 0, blend_dst_rgb = 0, blend_src_a = 0, blend_dst_a = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_a);
    GLint viewport[4] = {0,0,0,0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLboolean scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    GLint scissor_box[4] = {0,0,0,0};
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    GLint color_writemask[4] = {0,0,0,0};
    glGetIntegerv(GL_COLOR_WRITEMASK, color_writemask);

    GLint array_buffer = 0, elem_array_buffer = 0, vao = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elem_array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    // attrib 0..3 = raylib's fixed vertex/texcoord/normal/color locations in
    // its default shader (rlgl.h RL_DEFAULT_SHADER_ATTRIB_LOCATION_*).
    GLint attrib_enabled[4] = {-1,-1,-1,-1};
    if (auto* fn = r33_glGetVertexAttribiv()) {
        for (int i = 0; i < 4; i++) fn((unsigned)i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled[i]);
    }

    spdlog::info("[r33glstate] site={} blend={} depthTest={} depthMask={} stencilTest={} "
                 "cullFace={} cullFaceMode=0x{:X} frontFace=0x{:X} currentProgram={} "
                 "activeTexture=0x{:X} texBinding2D={} blendFactors=({},{},{},{}) "
                 "viewport=({},{},{},{}) scissorTest={} scissorBox=({},{},{},{}) fbo={} "
                 "colorWriteMask=({},{},{},{}) arrayBuffer={} elemArrayBuffer={} vao={} "
                 "attribEnabled=({},{},{},{})",
                 label, (bool)blend, (bool)depth_test, (bool)depth_mask, (bool)stencil_test,
                 (bool)cull_face, cull_face_mode, front_face, current_program,
                 active_texture, tex_binding_2d, blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a,
                 viewport[0], viewport[1], viewport[2], viewport[3],
                 (bool)scissor_test, scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3], fbo,
                 (bool)color_writemask[0], (bool)color_writemask[1], (bool)color_writemask[2], (bool)color_writemask[3],
                 array_buffer, elem_array_buffer, vao,
                 attrib_enabled[0], attrib_enabled[1], attrib_enabled[2], attrib_enabled[3]);
}
// END TEMP ROUND33 DIAGNOSTIC (declaration)

static void draw_model_face_last(ray::Model& model, int face_material_index, ray::Vector3 position, float scale) {
    ray::Matrix matTransform = ray::MatrixMultiply(ray::MatrixScale(scale, scale, scale),
                                                     ray::MatrixTranslate(position.x, position.y, position.z));
    ray::Matrix transform = ray::MatrixMultiply(model.transform, matTransform);

    for (int i = 0; i < model.meshCount; i++) {
        if (model.meshMaterial[i] == face_material_index) continue;
        ray::DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], transform);
    }
    if (face_material_index != -1) {
        for (int i = 0; i < model.meshCount; i++) {
            if (model.meshMaterial[i] == face_material_index)
                ray::DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], transform);
        }
    }
}

static ray::Matrix rotation_xyz(float ax, float ay, float az) {
    float cx = cosf(-ax), sx = sinf(-ax);
    float cy = cosf(-ay), sy = sinf(-ay);
    float cz = cosf(-az), sz = sinf(-az);
    ray::Matrix r = {};
    r.m0 = cz*cy;  r.m1 = (cz*sy*sx) - (sz*cx);  r.m2 = (cz*sy*cx) + (sz*sx);
    r.m4 = sz*cy;  r.m5 = (sz*sy*sx) + (cz*cx);   r.m6 = (sz*sy*cx) - (cz*sx);
    r.m8 = -sy;    r.m9 = cy*sx;                   r.m10 = cy*cx;
    r.m15 = 1.0f;
    return r;
}

static void reindex_animations(ray::Model& model, ray::Model& glb_model,
                               ray::ModelAnimation* anims, int anim_count) {
    std::unordered_map<std::string, int> glb_bone_idx;
    for (int i = 0; i < glb_model.skeleton.boneCount; i++)
        glb_bone_idx[glb_model.skeleton.bones[i].name] = i;

    int n = model.skeleton.boneCount;

    for (int a = 0; a < anim_count; a++) {
        auto& anim = anims[a];
        ray::ModelAnimPose* new_poses =
            (ray::ModelAnimPose*)std::malloc(anim.keyframeCount * sizeof(ray::ModelAnimPose));

        for (int f = 0; f < anim.keyframeCount; f++) {
            new_poses[f] = (ray::Transform*)std::malloc(n * sizeof(ray::Transform));
            for (int b = 0; b < n; b++) {
                auto it = glb_bone_idx.find(model.skeleton.bones[b].name);
                if (it != glb_bone_idx.end() && it->second < anim.boneCount)
                    new_poses[f][b] = anim.keyframePoses[f][it->second];
                else
                    new_poses[f][b] = model.skeleton.bindPose[b];
            }
            std::free(anim.keyframePoses[f]);
        }
        std::free(anim.keyframePoses);
        anim.keyframePoses = new_poses;
        anim.boneCount = n;
    }
}

static std::string name_lower(const char* s) {
    std::string r(s);
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

static std::unordered_map<std::string, int> parse_glb_material_indices(
        const std::string& path, std::vector<int>& recolor_out, int& face_out,
        std::vector<int>& additive_out, std::vector<int>& force_opaque_out) {
    std::unordered_map<std::string, int> result;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return result;

    uint32_t magic = 0, version = 0, total_len = 0;
    fread(&magic,     4, 1, f);
    fread(&version,   4, 1, f);
    fread(&total_len, 4, 1, f);

    if (magic != 0x46546C67u) { fclose(f); return result; }

    uint32_t chunk_len = 0, chunk_type = 0;
    fread(&chunk_len,  4, 1, f);
    fread(&chunk_type, 4, 1, f);

    if (chunk_type != 0x4E4F534Au) { fclose(f); return result; }

    std::string json(chunk_len, '\0');
    fread(json.data(), 1, chunk_len, f);
    fclose(f);

    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.HasParseError() || !doc.HasMember("materials")) return result;

    const auto& materials = doc["materials"];
    for (rapidjson::SizeType i = 0; i < materials.Size(); i++) {
        int raylib_idx = static_cast<int>(i) + 1;
        const char* mat_name_raw = nullptr;
        if (materials[i].HasMember("name") && materials[i]["name"].IsString()) {
            mat_name_raw = materials[i]["name"].GetString();
            result[mat_name_raw] = raylib_idx;
        }
        if (materials[i].HasMember("extras") && materials[i]["extras"].IsObject()) {
            const auto& extras = materials[i]["extras"];
            if (extras.HasMember("shaderType") && extras["shaderType"].IsString()) {
                std::string shader = extras["shaderType"].GetString();
                if (shader == "taikoEffectChangeColors")
                    recolor_out.push_back(raylib_idx);
                else if (shader == "taikoEffectFace" && face_out == -1)
                    face_out = raylib_idx;
            }
        }
        if (mat_name_raw) {
            std::string nl = name_lower(mat_name_raw);
            if (nl.find("_aa_add") != std::string::npos)
                additive_out.push_back(raylib_idx);
            else if (nl.find("_color_s_cus_") != std::string::npos &&
                     nl.find("_a_ab") == std::string::npos)
                force_opaque_out.push_back(raylib_idx);
        }
    }
    return result;
}

static void normalize_face_mesh_size(ray::Mesh& mesh, float target_size) {
    if (mesh.vertexCount == 0 || !mesh.vertices) return;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int v = 0; v < mesh.vertexCount; v++) {
        float x = mesh.vertices[v * 3 + 0], y = mesh.vertices[v * 3 + 1], z = mesh.vertices[v * 3 + 2];
        minx = std::min(minx, x); maxx = std::max(maxx, x);
        miny = std::min(miny, y); maxy = std::max(maxy, y);
        minz = std::min(minz, z); maxz = std::max(maxz, z);
    }
    float cx = (minx + maxx) / 2, cy = (miny + maxy) / 2, cz = (minz + maxz) / 2;
    float size = std::max(maxx - minx, maxy - miny);
    if (size <= 0.0001f) return;
    float factor = target_size / size;
    for (int v = 0; v < mesh.vertexCount; v++) {
        mesh.vertices[v * 3 + 0] = cx + (mesh.vertices[v * 3 + 0] - cx) * factor;
        mesh.vertices[v * 3 + 1] = cy + (mesh.vertices[v * 3 + 1] - cy) * factor;
        mesh.vertices[v * 3 + 2] = cz + (mesh.vertices[v * 3 + 2] - cz) * factor;
    }
}

void Chara3D::load_part(const fs::path& model_path, const fs::path& anim_path, bool normalize_face_scale) {
    ray::Model model = ray::LoadModel(model_path.string().c_str());
    for (int m = 0; m < model.meshCount; m++) {
        auto& mesh = model.meshes[m];
        if (mesh.colors == nullptr) continue;
        for (int v = 0; v < mesh.vertexCount * 4; v++) mesh.colors[v] = 255;
        ray::UpdateMeshBuffer(mesh, 3, mesh.colors, mesh.vertexCount * 4, 0);
    }

    std::vector<int> recolor_indices, additive_indices, force_opaque_indices;
    int face_material_index = -1;
    auto material_indices = parse_glb_material_indices(model_path.string(), recolor_indices, face_material_index, additive_indices, force_opaque_indices);

    if (normalize_face_scale && face_material_index != -1) {
        constexpr float COS_FACE_PLANE_SIZE = 0.137f;
        for (int m = 0; m < model.meshCount; m++)
            if (model.meshMaterial[m] == face_material_index)
                normalize_face_mesh_size(model.meshes[m], COS_FACE_PLANE_SIZE);
    }
#ifdef PLATFORM_ANDROID
    if (face_material_index != -1 && face_shader.id != 0)
        model.materials[face_material_index].shader = face_shader;
#endif
    additive_indices.erase(
        std::remove(additive_indices.begin(), additive_indices.end(), face_material_index),
        additive_indices.end());
    for (int idx : additive_indices)
        model.materials[idx].maps[ray::MATERIAL_MAP_DIFFUSE].color = {255, 255, 255, 255};
    for (int idx : force_opaque_indices) {
        auto& map = model.materials[idx].maps[ray::MATERIAL_MAP_DIFFUSE];
        if (map.texture.id != 0) {
            ray::Image img = ray::LoadImageFromTexture(map.texture);
            ray::ImageFormat(&img, ray::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            unsigned char* px = (unsigned char*)img.data;
            for (int p = 0; p < img.width * img.height; p++) px[p * 4 + 3] = 255;
            ray::UnloadTexture(map.texture);
            map.texture = ray::LoadTextureFromImage(img);
            ray::UnloadImage(img);
        }
    }

    ray::Model glb_model = ray::LoadModel(anim_path.string().c_str());
    int anim_count = 0;
    ray::ModelAnimation* anims = ray::LoadModelAnimations(anim_path.string().c_str(), &anim_count);
    reindex_animations(model, glb_model, anims, anim_count);
    ray::UnloadModel(glb_model);

    parts.push_back(model);
    part_material_indices.push_back(std::move(material_indices));
    part_recolor_indices.push_back(std::move(recolor_indices));
    part_additive_indices.push_back(std::move(additive_indices));
    part_force_opaque_indices.push_back(std::move(force_opaque_indices));
    part_face_material_index.push_back(face_material_index);
    part_anims.push_back(anims);
    part_anim_count.push_back(anim_count);
}

static void init_shaders(ray::Shader& fxaa_shader, int& fxaa_size_loc,
                          ray::Shader& outline_pass_shader, int& outline_pass_size_loc, int& outline_pass_thickness_loc,
                          ray::Shader& null_shader, ray::Shader& face_shader, ray::Shader& outline_shader,
                          bool& use_render_textures) {
    fxaa_shader   = load_shader("shader/pass.vs", "shader/fxaa.fs");
    fxaa_size_loc = ray::GetShaderLocation(fxaa_shader, "texSize");

    outline_pass_shader = load_shader("shader/pass.vs", "shader/outline_pass.fs");
    outline_pass_size_loc = ray::GetShaderLocation(outline_pass_shader, "texSize");
    outline_pass_thickness_loc = ray::GetShaderLocation(outline_pass_shader, "outlineThickness");
    float outline_thickness = 3.0f;
    ray::SetShaderValue(outline_pass_shader, outline_pass_thickness_loc, &outline_thickness, ray::SHADER_UNIFORM_FLOAT);

    null_shader    = load_shader(nullptr, "shader/null.fs");
    face_shader    = load_shader(nullptr, "shader/face.fs");
    outline_shader = load_shader("shader/outline.vs", "shader/outline.fs");
    int thickness_loc = ray::GetShaderLocation(outline_shader, "outlineThickness");
    float thickness = 0.0035f;
    ray::SetShaderValue(outline_shader, thickness_loc, &thickness, ray::SHADER_UNIFORM_FLOAT);

    if (fxaa_shader.id == 0 || outline_pass_shader.id == 0)
        use_render_textures = false;
}

Chara3D::Chara3D(std::string& model_name, bool mirror) {
    init_shaders(fxaa_shader, fxaa_size_loc, outline_pass_shader, outline_pass_size_loc, outline_pass_thickness_loc,
                 null_shader, face_shader, outline_shader, use_render_textures);
    // TEMP ROUND30 DIAGNOSTIC - bisecting the GAME-only invisibility bug by
    // forcing the direct-render fallback path instead of the cached
    // render-texture/FXAA/outline-pass chain. Must be removed before finishing.
    if (getenv("YATAIDON_R30_NO_RT")) use_render_textures = false;
    this->mirror = mirror;

    // Models has no inheritance mechanism of its own (unlike Graphics) ??each asset
    // resolves against the child skin first, falling back to the parent's.
    fs::path model_path = tex.resolve_skin_path(fs::path("Models/cos") / (model_name + ".glb"));
    fs::path anim_path  = tex.resolve_skin_path("Models/animations.glb");
    load_part(model_path, anim_path);

    model_valid = parts[0].meshCount > 0;

    fs::path face_dir = tex.resolve_skin_path("Models/face");
    load_face_textures(face_dir);

    fs::path skin_anim_path = fs::path("Skins") / global_data.config->paths.skin
                              / "Graphics" / "global" / "animation.json";
    load_face_anims(skin_anim_path);

    set_anim(anim_index);
    prewarm_render_targets();   // ROUND 103
}

Chara3D::Chara3D(std::string& head_name, std::string& body_name, bool mirror) {
    init_shaders(fxaa_shader, fxaa_size_loc, outline_pass_shader, outline_pass_size_loc, outline_pass_thickness_loc,
                 null_shader, face_shader, outline_shader, use_render_textures);
    // TEMP ROUND30 DIAGNOSTIC - see the model_name ctor above.
    if (getenv("YATAIDON_R30_NO_RT")) use_render_textures = false;
    this->mirror = mirror;

    fs::path head_path = tex.resolve_skin_path(fs::path("Models/head") / (head_name + ".glb"));
    fs::path body_path = tex.resolve_skin_path(fs::path("Models/body") / (body_name + ".glb"));
    fs::path anim_path = tex.resolve_skin_path("Models/animations.glb");
    load_part(body_path, anim_path);
    load_part(head_path, anim_path, true);

    model_valid = parts[0].meshCount > 0 && parts[1].meshCount > 0;

    fs::path face_dir = tex.resolve_skin_path("Models/face");
    load_face_textures(face_dir);

    fs::path skin_anim_path = fs::path("Skins") / global_data.config->paths.skin
                              / "Graphics" / "global" / "animation.json";
    load_face_anims(skin_anim_path);

    set_anim(anim_index);
    prewarm_render_targets();   // ROUND 103
}

Chara3D::~Chara3D() {
    for (size_t p = 0; p < parts.size(); p++) {
        if (part_face_material_index[p] != -1 && !face_textures.empty())
            parts[p].materials[part_face_material_index[p]].maps[ray::MATERIAL_MAP_DIFFUSE].texture.id = 0;
        ray::UnloadModelAnimations(part_anims[p], part_anim_count[p]);
        ray::UnloadModel(parts[p]);
    }
    ray::UnloadShader(fxaa_shader);
    ray::UnloadShader(null_shader);
    ray::UnloadShader(face_shader);
    ray::UnloadShader(outline_pass_shader);
    if (fxaa_target.id != 0) ray::UnloadRenderTexture(fxaa_target);
    if (scene_target.id != 0) ray::UnloadRenderTexture(scene_target);
    ray::UnloadShader(outline_shader);
    for (auto& tex : face_textures)
        ray::UnloadTexture(tex);
}

void Chara3D::set_texture(fs::path& texture_path, int part_index, int material_index) {
    ray::Texture2D old = parts[part_index].materials[material_index].maps[ray::MATERIAL_MAP_DIFFUSE].texture;
    if (old.id != 0) ray::UnloadTexture(old);
    ray::Texture tex = ray::LoadTexture(texture_path.string().c_str());
    ray::GenTextureMipmaps(&tex);
    ray::SetTextureFilter(tex, ray::TEXTURE_FILTER_BILINEAR);
    int map_type = ray::MATERIAL_MAP_DIFFUSE;
    ray::SetMaterialTexture(&parts[part_index].materials[material_index], map_type, tex);
    render_dirty = true;
}

void Chara3D::set_body_texture(fs::path& texture_path) {
    for (size_t p = 0; p < parts.size(); p++) {
        auto it = part_material_indices[p].find("RGB_don_color_S_CUS_0x10000001_");
        if (it != part_material_indices[p].end()) { set_texture(texture_path, (int)p, it->second); return; }
    }
}

void Chara3D::set_face_rim_texture(fs::path& texture_path) {
    for (size_t p = 0; p < parts.size(); p++) {
        auto it = part_material_indices[p].find("don_FACEHIP_color_S_CUS_0x10000001_");
        if (it != part_material_indices[p].end()) { set_texture(texture_path, (int)p, it->second); return; }
    }
}

void Chara3D::load_face_textures(fs::path& face_dir) {
    if (!fs::exists(face_dir)) return;
    std::vector<fs::path> paths;
    for (auto& e : fs::directory_iterator(face_dir)) {
        if (e.path().extension() == ".png")
            paths.push_back(e.path());
    }
    if (paths.empty()) return;
    std::sort(paths.begin(), paths.end());
    ray::Image sheet = ray::LoadImage(paths[0].string().c_str());
    // Frames are square and stacked vertically; derive the size from the
    // sheet width instead of assuming 128px, so higher-resolution skins
    // slice on the right boundaries (the UVs are normalized anyway).
    int frame_size = sheet.width;
    int frame_count = frame_size > 0 ? sheet.height / frame_size : 0;
    for (int f = 0; f < frame_count; f++) {
        ray::Rectangle rect = {0, (float)(f * frame_size), (float)frame_size, (float)frame_size};
        ray::Image frame_img = ray::ImageFromImage(sheet, rect);
        face_textures.push_back(ray::LoadTextureFromImage(frame_img));
        ray::UnloadImage(frame_img);
    }
    ray::UnloadImage(sheet);
    apply_face(0);
}

void Chara3D::load_face_anims(fs::path& anim_path) {
    if (!fs::exists(anim_path)) return;
    std::ifstream f(anim_path.string());
    if (!f.is_open()) return;
    std::string json_str((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    AnimationParser parser;
    face_anims = parser.parseAnimationsFromString(json_str);
}

void Chara3D::apply_face(int face_index) {
    if (face_index < 0 || face_index >= (int)face_textures.size()) return;
    for (size_t p = 0; p < parts.size(); p++) {
        if (part_face_material_index[p] == -1) continue;
        parts[p].materials[part_face_material_index[p]].maps[ray::MATERIAL_MAP_DIFFUSE].texture =
            face_textures[face_index];
    }
    current_face_index = face_index;
    render_dirty = true;
}

static ray::Texture2D recolor_texture(ray::Image& source,
                                       ray::Color body, ray::Color face, ray::Color rim) {
    ray::Image img = ray::ImageCopy(source);
    ray::ImageFormat(&img, ray::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    unsigned char* pixels = (unsigned char*)img.data;
    int total = img.width * img.height;

    for (int i = 0; i < total; i++) {
        float r = pixels[i * 4 + 0] / 255.0f;
        float g = pixels[i * 4 + 1] / 255.0f;
        float b = pixels[i * 4 + 2] / 255.0f;

        float strongest = fmaxf(r, fmaxf(g, b));
        float weakest   = fminf(r, fminf(g, b));
        if (strongest <= 0.05f || (strongest - weakest) <= 0.08f) continue;

        ray::Color out;
        if (b > r && b >= g)     out = rim;
        else if (g > r && g > b) out = face;
        else                      out = body;

        pixels[i * 4 + 0] = out.r;
        pixels[i * 4 + 1] = out.g;
        pixels[i * 4 + 2] = out.b;
    }

    ray::Texture2D result = ray::LoadTextureFromImage(img);
    ray::UnloadImage(img);
    return result;
}

static void apply_don_colors(ray::Model& model, int mat_idx,
                              ray::Color body, ray::Color face, ray::Color rim) {
    auto& map = model.materials[mat_idx].maps[ray::MATERIAL_MAP_DIFFUSE];
    ray::Image img = ray::LoadImageFromTexture(map.texture);
    ray::Texture2D new_tex = recolor_texture(img, body, face, rim);
    ray::UnloadImage(img);
    ray::UnloadTexture(map.texture);
    map.texture = new_tex;
}

void Chara3D::set_don_colors(ray::Color body, ray::Color face, ray::Color rim) {
    for (size_t p = 0; p < parts.size(); p++)
        for (int idx : part_recolor_indices[p])
            apply_don_colors(parts[p], idx, body, face, rim);
    render_dirty = true;
}

static constexpr int FACE_ANIM_IDS[] = {
    13, 14, 15, 16, 65, 17, 22, 19, 30, 29, 23, 24, 63, 44,
    40, 41, 42, 43, 44, 45, 46, 58, 59, 62, 60, 18,
    13, 14, 15, 16, 65, 17, 22, 19, 30, 29, 23, 24, 63, 44,
    41, 40, 42, 43, 44, 45, 46, 58, 59, 62, 60, 18,
};

void Chara3D::set_anim(AnimIndex idx) {
    int i = static_cast<int>(idx);
    int anim_count = part_anim_count.empty() ? 0 : part_anim_count[0];
    // ROUND (r-chara3d-fullanim): cheap, always-on (not diagnostic-gated --
    // this only fires on an actual pose CHANGE, not per-frame) observability
    // for which pose the mascot switches to and when. Added specifically so
    // this round's newly-wired DON_FULL_COMBO/DON_FULL_GAGE triggers (and
    // any future pose-wiring work) can be confirmed from a plain automation
    // log grep instead of needing a pixel-readback capture every time.
    spdlog::info("[chara3d] set_anim idx={} name={}", i, i >= 0 ? get_anim_name(i) : "?");
    if (i >= 0 && i < anim_count) {
        if (idx == AnimIndex::DON_NORMAL || idx == AnimIndex::DON_SABI) {
            is_looping = true;
        } else if (get_anim_name(i).find("loop") == std::string::npos) {
            is_looping = false;
            prev_anim_idx = anim_index;
        }
        anim_index = idx;
        anim_frame = 0;
        last_frame_ms = 0;
        render_dirty = true;
    }

    if (i >= 0 && i < (int)(sizeof(FACE_ANIM_IDS) / sizeof(FACE_ANIM_IDS[0]))) {
        auto it = face_anims.find(FACE_ANIM_IDS[i]);
        if (it != face_anims.end()) {
            current_face_anim = it->second.get();
            current_face_anim->reset();
            current_face_anim->start();
            apply_face((int)current_face_anim->attribute);
        }
    }
}

std::string Chara3D::get_anim_name(int idx) {
    if (!part_anims.empty() && idx >= 0 && idx < part_anim_count[0]) {
        return part_anims[0][idx].name;
    }
    return "";
}

void Chara3D::set_bpm(float bpm) {
    this->bpm = bpm;
}

int Chara3D::get_anim_count() const {
    return part_anim_count.empty() ? 0 : part_anim_count[0];
}

void Chara3D::update(double current_ms) {
    int anim_count = part_anim_count.empty() ? 0 : part_anim_count[0];
    if (anim_count > 0) {
        int ai = static_cast<int>(anim_index);
        double ms_per_beat = 60000.0 / bpm;
        if (anim_index == AnimIndex::DON_NORMAL || anim_index == AnimIndex::DON_SABI) ms_per_beat *= 3;
        if (anim_index == AnimIndex::DON_BALLOON_LOOP) ms_per_beat /= 2;
        double ms_per_frame = ms_per_beat / part_anims[0][ai].keyframeCount;
        if (current_ms - last_frame_ms >= ms_per_frame) {
            int loop_frames = part_anims[0][ai].keyframeCount - 1;
            double prev_last_frame_ms = last_frame_ms;  // TEMP ROUND40 DIAGNOSTIC
            last_frame_ms = current_ms;

            if (loop_frames <= 0) {
                if (!is_looping) {
                    set_anim(prev_anim_idx);
                    is_looping = true;
                }
            } else {
                anim_frame = (anim_frame + 1) % loop_frames;
                // UpdateModelAnimation CPU-skins and uploads position/normal
                // buffers to the GPU itself; no manual UpdateMeshBuffer needed
                for (size_t p = 0; p < parts.size(); p++)
                    ray::UpdateModelAnimation(parts[p], part_anims[p][ai], anim_frame);
                render_dirty = true;

                // TEMP ROUND40 DIAGNOSTIC (r40-gamedan-darkband-recheck) - is
                // the idle loop actually advancing at the expected real-time
                // cadence, or does it fall behind (e.g. automation shot()
                // stalls collapsing a large current_ms jump into a single
                // +1 frame step, per chara_3d.cpp:616's `if`, not `while`)?
                // Reused gate, no output-affecting change, same pattern as
                // ROUND 33/34's own diagnostics in this file.
                if (getenv("YATAIDON_R33_GLSTATE")) {
                    spdlog::info("[r40animclock] anim_idx={} frame={}/{} current_ms={:.1f} "
                                 "gap_ms={:.1f} ms_per_frame={:.2f} bpm={:.1f}",
                                 ai, anim_frame, loop_frames, current_ms,
                                 current_ms - prev_last_frame_ms, ms_per_frame, bpm);
                }

                if (!is_looping && anim_frame == loop_frames - 1) {
                    set_anim(prev_anim_idx);
                    is_looping = true;
                }
            }
        }
    }

    if (current_face_anim) {
        current_face_anim->update(current_ms);
        int new_face = (int)current_face_anim->attribute;
        if (new_face != current_face_index)
            apply_face(new_face);
    }
}

void Chara3D::draw_outline(float x, float y) {
    std::vector<std::vector<ray::Shader>> saved(parts.size());
    for (size_t p = 0; p < parts.size(); p++) {
        saved[p].resize(parts[p].materialCount);
        for (int i = 0; i < parts[p].materialCount; i++) {
            saved[p][i] = parts[p].materials[i].shader;
            bool is_face = (part_face_material_index[p] != -1 && i == part_face_material_index[p] && null_shader.id != 0);
            parts[p].materials[i].shader = is_face ? null_shader : outline_shader;
        }
    }

    std::vector<ray::Matrix> saved_transform(parts.size());
    float y_angle = mirror ? -rot_y : rot_y;
    ray::Matrix rot = rotation_xyz(rot_x * DEG2RAD, y_angle * DEG2RAD, rot_z * DEG2RAD);
    for (size_t p = 0; p < parts.size(); p++) {
        saved_transform[p] = parts[p].transform;
        parts[p].transform = rot;
    }

    rlSetCullFace(RL_CULL_FACE_FRONT);
    // scale is in 1280x720 virtual units; the camera maps the skin's virtual
    // canvas to the window, so follow the skin resolution or the model
    // shrinks relative to everything else on hi-res skins.
    for (auto& part : parts)
        ray::DrawModel(part, {x, y, 400.0f}, scale * draw_scale * tex.screen_scale, ray::WHITE);
    rlSetCullFace(RL_CULL_FACE_BACK);

    for (size_t p = 0; p < parts.size(); p++) {
        parts[p].transform = saved_transform[p];
        for (int i = 0; i < parts[p].materialCount; i++)
            parts[p].materials[i].shader = saved[p][i];
    }
}

void Chara3D::draw_3d(float x, float y) {
    std::vector<ray::Matrix> saved(parts.size());
    float y_angle = mirror ? -rot_y : rot_y;
    ray::Matrix rot = rotation_xyz(rot_x * DEG2RAD, y_angle * DEG2RAD, rot_z * DEG2RAD);
    for (size_t p = 0; p < parts.size(); p++) {
        saved[p] = parts[p].transform;
        parts[p].transform = rot;
    }
    for (size_t p = 0; p < parts.size(); p++)
        draw_model_face_last(parts[p], part_face_material_index[p], {x, y, 400.0f}, scale * draw_scale * tex.screen_scale);
    for (size_t p = 0; p < parts.size(); p++)
        parts[p].transform = saved[p];
}

// TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - extends ROUND33's GL-state
// dump with the actual uniform/matrix data feeding this exact draw call:
// the model transform (rotation) chara_3d.cpp itself computes, raylib's live
// rlGetMatrixModelview()/rlGetMatrixProjection() at the moment of the draw,
// the full Camera3D struct camera2d_to_3d() built, and mesh0's own
// vertex-count/bounding-box/bound-texture-id (not just "non-zero"). Same
// gate as ROUND33 (YATAIDON_R33_GLSTATE) - no new env var invented. See
// Graphics/game/MAPPING_hud.md ROUND 34 and ENGINE_BINDINGS.md ROUND 34.
static void r34_dump_matrix_state(const char* label, float x, float y, float z, float model_scale,
                                   const ray::Matrix& rot, const ray::Camera3D& cam3d, ray::Model* mesh_model) {
    if (!getenv("YATAIDON_R33_GLSTATE")) return;

    ray::Matrix mv   = rlGetMatrixModelview();
    ray::Matrix proj = rlGetMatrixProjection();

    int vcount = -1;
    unsigned int tex_id = 0xFFFFFFFFu;
    float minx = 0, maxx = 0, miny = 0, maxy = 0, minz = 0, maxz = 0;
    if (mesh_model && mesh_model->meshCount > 0) {
        auto& mesh = mesh_model->meshes[0];
        vcount = mesh.vertexCount;
        int mat_idx = mesh_model->meshMaterial[0];
        tex_id = mesh_model->materials[mat_idx].maps[ray::MATERIAL_MAP_DIFFUSE].texture.id;
        if (mesh.vertices && mesh.vertexCount > 0) {
            minx = miny = minz = 1e9f;
            maxx = maxy = maxz = -1e9f;
            for (int v = 0; v < mesh.vertexCount; v++) {
                float vx = mesh.vertices[v * 3 + 0], vy = mesh.vertices[v * 3 + 1], vz = mesh.vertices[v * 3 + 2];
                minx = std::min(minx, vx); maxx = std::max(maxx, vx);
                miny = std::min(miny, vy); maxy = std::max(maxy, vy);
                minz = std::min(minz, vz); maxz = std::max(maxz, vz);
            }
        }
    }

    spdlog::info("[r34matrix] site={} drawPos=({:.2f},{:.2f},{:.2f}) drawScale={:.4f} "
                 "rot=[{:.3f} {:.3f} {:.3f} {:.3f} / {:.3f} {:.3f} {:.3f} {:.3f} / {:.3f} {:.3f} {:.3f} {:.3f}] "
                 "camPos=({:.2f},{:.2f},{:.2f}) camTarget=({:.2f},{:.2f},{:.2f}) camUp=({:.3f},{:.3f},{:.3f}) "
                 "camFovy={:.3f} camProjection={} "
                 "rlModelview=[{:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f}] "
                 "rlProjection=[{:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f} / {:.4f} {:.4f} {:.4f} {:.4f}] "
                 "mesh0Vertices={} mesh0Bounds=({:.4f},{:.4f},{:.4f})-({:.4f},{:.4f},{:.4f}) mesh0TexId={}",
                 label, x, y, z, model_scale,
                 rot.m0, rot.m4, rot.m8, rot.m12, rot.m1, rot.m5, rot.m9, rot.m13, rot.m2, rot.m6, rot.m10, rot.m14,
                 cam3d.position.x, cam3d.position.y, cam3d.position.z,
                 cam3d.target.x, cam3d.target.y, cam3d.target.z,
                 cam3d.up.x, cam3d.up.y, cam3d.up.z,
                 cam3d.fovy, (int)cam3d.projection,
                 mv.m0, mv.m4, mv.m8, mv.m12, mv.m1, mv.m5, mv.m9, mv.m13, mv.m2, mv.m6, mv.m10, mv.m14, mv.m3, mv.m7, mv.m11, mv.m15,
                 proj.m0, proj.m4, proj.m8, proj.m12, proj.m1, proj.m5, proj.m9, proj.m13, proj.m2, proj.m6, proj.m10, proj.m14, proj.m3, proj.m7, proj.m11, proj.m15,
                 vcount, minx, miny, minz, maxx, maxy, maxz, tex_id);
}
// TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - the next probe ROUND33 flagged:
// read back the actual rasterized pixels of a render texture right after the
// draw that fills it, to separate "wrong transform" (pixels drawn, wrong
// place) from "draw call never actually executed/discarded" (zero
// non-transparent pixels anywhere). Same YATAIDON_R33_GLSTATE gate, no new
// env var. GPU readback is relatively expensive so this only ever runs
// inside the already-gated diagnostic path.
struct R34SamplePoint { int x = -1; int y = -1; };
static R34SamplePoint r34_dump_pixel_readback(const char* label, ray::RenderTexture2D& rt) {
    if (!getenv("YATAIDON_R33_GLSTATE")) return {};
    ray::Image img = ray::LoadImageFromTexture(rt.texture);
    if (img.data == nullptr) {
        spdlog::info("[r34pixels] site={} readback_failed", label);
        return {};
    }
    ray::ImageFormat(&img, ray::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    unsigned char* px = (unsigned char*)img.data;
    long total = (long)img.width * (long)img.height;
    long nonzero_alpha = 0;
    int max_alpha = 0;
    int min_x = -1, min_y = -1, max_x = -1, max_y = -1;
    for (int yy = 0; yy < img.height; yy++) {
        for (int xx = 0; xx < img.width; xx++) {
            unsigned char a = px[(yy * img.width + xx) * 4 + 3];
            if (a > 0) {
                nonzero_alpha++;
                if (a > max_alpha) max_alpha = a;
                if (min_x == -1 || xx < min_x) min_x = xx;
                if (min_y == -1 || yy < min_y) min_y = yy;
                if (xx > max_x) max_x = xx;
                if (yy > max_y) max_y = yy;
            }
        }
    }
    // Sample RGB at the center of the non-transparent bounds (image-space,
    // GL bottom-origin, same convention glReadPixels uses) so it can be
    // compared byte-for-byte against a backbuffer sample at the same point.
    int cx = -1, cy = -1;
    unsigned char sr = 0, sg = 0, sb = 0, sa = 0;
    if (min_x != -1) {
        cx = (min_x + max_x) / 2;
        cy = (min_y + max_y) / 2;
        size_t idx = ((size_t)cy * img.width + cx) * 4;
        sr = px[idx]; sg = px[idx + 1]; sb = px[idx + 2]; sa = px[idx + 3];
    }
    spdlog::info("[r34pixels] site={} texW={} texH={} nonTransparentPixels={}/{} maxAlpha={} "
                 "nonTransparentBounds=({},{})-({},{}) centerSample=({},{})rgba=({},{},{},{})",
                 label, img.width, img.height, nonzero_alpha, total, max_alpha,
                 min_x, min_y, max_x, max_y, cx, cy, sr, sg, sb, sa);
    ray::UnloadImage(img);
    return {cx, cy};
}

// TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - the decisive follow-up: is
// the chara's own final on-screen composite blit (DrawTextureRec(fxaa_target,
// ...) right before this function is called) actually reaching the real,
// presented default framebuffer? r34_dump_pixel_readback already proved the
// OFFSCREEN fxaa_target texture holds fully-opaque, correctly-positioned
// pixels immediately before that blit call. This reads back the SAME region
// of the real backbuffer (GL bottom-origin, so the same (x,y) as the
// fxaa_target sample above is directly comparable) right after the blit, to
// tell "never reached the screen" apart from "reached the screen but
// something drawn later overwrote it" - both would look identical in every
// earlier diagnostic (GL state, matrices, offscreen texture content) but are
// very different bugs to chase next.
static void r34_dump_backbuffer_sample(const char* label, int cx, int cy) {
    if (!getenv("YATAIDON_R33_GLSTATE")) return;
    if (cx < 0 || cy < 0) {
        spdlog::info("[r34backbuf] site={} no_sample_point", label);
        return;
    }
    rlDrawRenderBatchActive();  // force the just-queued blit to actually execute
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    spdlog::info("[r34backbuf] site={} fbo={} sample=({},{}) rgba=({},{},{},{})",
                 label, prev_fbo, cx, cy, px[0], px[1], px[2], px[3]);
}
// END TEMP ROUND34 DIAGNOSTIC (declaration)

// ---------------------------------------------------------------------------
// ROUND 103 (r103-frametime): `scene_target` and `fxaa_target` used to be
// created INSIDE draw(), on the first frame this Chara3D was ever drawn.
//
// That is a lazy full-screen GPU allocation on a draw path, and it is one of
// the things the user meant by ??憭??怠蝚砌?甈∟??亦????～? Each
// Chara3D owns TWO of them, and a 2P game has two Chara3D instances, so four
// full-screen render targets (colour + depth each) were being allocated in
// the middle of gameplay frames.
//
// Measured on `build/bin/YataiDON.exe` (03:49, -O2), 2P, target_fps = 120,
// ~11,000 game frames per run, using the existing env gates:
//
//   mascot off  (YATAIDON_R34_NOCHARA)  draw p50 0.156  p99 0.631  MAX  2.05 ms
//   no RT chain (YATAIDON_R30_NO_RT)    draw p50 0.490  p99 1.895  MAX  9.22 ms
//   shipped (RT chain on)               draw p50 0.832  p99 1.881  MAX 22.37 ms
//                                       (and MAX 532.84 ms in another run)
//
// The fix is PREWARM, not "make the allocation faster": the targets are now
// created in the constructors, which run during screen init behind the
// loading screen / transition where a hitch is invisible. draw() still calls
// this, so a genuine render-size change is still handled - it just no longer
// pays for the FIRST one mid-gameplay.
//
// `YATAIDON_R103_DISABLE` restores the old lazy-on-first-draw behaviour, so a
// before/after A/B comes from ONE binary instead of two (this project has been
// burned by a stale exe making an A/B two runs of identical code). Same
// house pattern as YATAIDON_R45_DISABLE / R48_DISABLE / R55_DISABLE / R76_DISABLE.
// ---------------------------------------------------------------------------
void Chara3D::ensure_render_targets(int rw, int rh) {
    if (rw <= 0 || rh <= 0) return;

    if (scene_target.id == 0 || scene_target_w != rw || scene_target_h != rh) {
        perf::PerfTimer rt_timer;
        if (scene_target.id != 0) ray::UnloadRenderTexture(scene_target);
        scene_target = ray::LoadRenderTexture(rw, rh);
        perf::note_event("chara3d_scene_target",
                         std::to_string(rw) + "x" + std::to_string(rh),
                         rt_timer.ms());
        if (scene_target.id == 0) {
            spdlog::warn("Chara3D: render texture unavailable, using direct render");
            use_render_textures = false;
        }
        scene_target_w = rw;
        scene_target_h = rh;
        float ts[2] = {(float)rw, (float)rh};
        ray::SetShaderValue(outline_pass_shader, outline_pass_size_loc, ts,
                            ray::SHADER_UNIFORM_VEC2);
        render_dirty = true;
    }

    // Only the render-texture path needs the FXAA target; the direct-render
    // fallback never touches it, so do not allocate one for it.
    if (!use_render_textures) return;

    if (fxaa_target.id == 0 || fxaa_target_w != rw || fxaa_target_h != rh) {
        perf::PerfTimer rt_timer;
        if (fxaa_target.id != 0) ray::UnloadRenderTexture(fxaa_target);
        fxaa_target = ray::LoadRenderTexture(rw, rh);
        perf::note_event("chara3d_fxaa_target",
                         std::to_string(rw) + "x" + std::to_string(rh),
                         rt_timer.ms());
        fxaa_target_w = rw;
        fxaa_target_h = rh;
        float ts[2] = {(float)rw, (float)rh};
        ray::SetShaderValue(fxaa_shader, fxaa_size_loc, ts, ray::SHADER_UNIFORM_VEC2);
        render_dirty = true;
    }
}

// ROUND 103: the prewarm call the two constructors make. Kept out of line so
// the `YATAIDON_R103_DISABLE` decision lives in exactly one place.
void Chara3D::prewarm_render_targets() {
    static const bool disabled = std::getenv("YATAIDON_R103_DISABLE") != nullptr;
    if (disabled) return;
    if (!model_valid) return;
    ensure_render_targets(ray::GetRenderWidth(), ray::GetRenderHeight());
}

void Chara3D::draw(float x, float y, float scale_mul, const char* debug_label) {
    if (!model_valid) return;
    // TEMP ROUND34 DIAGNOSTIC (r34-chara3d-matrix) - A/B probe: skip the
    // entire draw when set, so a screenshot with vs without this env var can
    // be pixel-diffed to isolate this call's ACTUAL visual contribution,
    // disambiguating "renders but is visually camouflaged against existing
    // 2D art" from "still doesn't render." Separate var from
    // YATAIDON_R33_GLSTATE on purpose - this changes rendered OUTPUT, not
    // just logging, so it must never be on by accident.
    if (getenv("YATAIDON_R34_NOCHARA")) return;

    if (scale_mul != draw_scale) {
        draw_scale   = scale_mul;
        render_dirty = true;
    }

    int rw = ray::GetRenderWidth();
    int rh = ray::GetRenderHeight();

    ensure_render_targets(rw, rh);

    if (!use_render_textures) {
        ray::Camera2D cam2d = compute_camera2d(tex.screen_width, tex.screen_height);
        ray::Camera3D cam3d = camera2d_to_3d(cam2d);
        ray::EndMode2D();
        ray::EndBlendMode();
        ray::BeginMode3D(cam3d);
        // TEMP ROUND33 DIAGNOSTIC - direct-render fallback path, no outline pass.
        r33_dump_gl_state(debug_label ? debug_label : "unlabeled(direct)");
        // TEMP ROUND34 DIAGNOSTIC - same call site, matrix/uniform data.
        {
            float y_angle_r34 = mirror ? -rot_y : rot_y;
            ray::Matrix rot_r34 = rotation_xyz(rot_x * DEG2RAD, y_angle_r34 * DEG2RAD, rot_z * DEG2RAD);
            r34_dump_matrix_state(debug_label ? debug_label : "unlabeled(direct)", x, y, 400.0f,
                                   scale * draw_scale * tex.screen_scale, rot_r34, cam3d,
                                   parts.empty() ? nullptr : &parts[0]);
        }
        draw_3d(x, y);
        ray::EndMode3D();
        ray::BeginBlendMode(ray::BLEND_CUSTOM_SEPARATE);
        ray::BeginMode2D(cam2d);
        return;
    }

    if (x != last_draw_x || y != last_draw_y) {
        last_draw_x = x;
        last_draw_y = y;
        render_dirty = true;
    }

    ray::Camera2D cam2d = compute_camera2d(tex.screen_width, tex.screen_height);

    // ROUND 103 second pass. The render-target hypothesis was WRONG (measured:
    // all four allocations cost 5.03 ms total and all land on the GAME screen's
    // frame 0, behind the transition), so these sub-timers exist to find what
    // in this draw actually costs the 327-561 ms the traces keep catching.
    // Only ever emits when a section is genuinely slow, so a normal frame adds
    // nothing to the log.
    const bool r103 = perf::events_enabled();
    perf::PerfTimer draw_timer;
    double t_scene = 0.0, t_outline_pass = 0.0;

    // ROUND 103 third pass. The second pass measured the whole call at 342.79 ms
    // with scene=0.4 / outline_pass=0.0 / blit=0.0 -- i.e. the cost is in NONE
    // of the three passes, so it has to be here. `EndMode2D()` is not a
    // bookkeeping call: raylib's EndMode2D calls rlDrawRenderBatchActive(),
    // which SUBMITS the whole frame's accumulated 2D batch. Whoever ends 2D
    // mode first is charged for every 2D draw the frame has queued so far,
    // which in GAME is the entire background + lane + notes + HUD.
    perf::PerfTimer flush_timer;
    ray::EndMode2D();
    ray::EndBlendMode();
    const double t_flush2d = r103 ? flush_timer.ms() : 0.0;

    // Model pose/textures/position only change on animation ticks (well below
    // display refresh), so the outline + composite passes are cached in
    // fxaa_target and re-rendered only when something actually changed
    if (render_dirty) {
        render_dirty = false;
        ray::Camera3D cam3d = camera2d_to_3d(cam2d);
        perf::PerfTimer scene_timer;   // ROUND 103

        ray::BeginTextureMode(scene_target);
        ray::ClearBackground(ray::BLANK);
        ray::BeginBlendMode(ray::BLEND_ALPHA);
        ray::BeginMode3D(cam3d);
        // TEMP ROUND33 DIAGNOSTIC - cached render-texture path.
        r33_dump_gl_state(debug_label ? debug_label : "unlabeled(cached)");
        // TEMP ROUND34 DIAGNOSTIC - same call site, matrix/uniform data.
        {
            float y_angle_r34 = mirror ? -rot_y : rot_y;
            ray::Matrix rot_r34 = rotation_xyz(rot_x * DEG2RAD, y_angle_r34 * DEG2RAD, rot_z * DEG2RAD);
            r34_dump_matrix_state(debug_label ? debug_label : "unlabeled(cached)", x, y, 400.0f,
                                   scale * draw_scale * tex.screen_scale, rot_r34, cam3d,
                                   parts.empty() ? nullptr : &parts[0]);
        }
        draw_outline(x, y);
        draw_3d(x, y);
        ray::EndMode3D();
        ray::EndBlendMode();
        ray::EndTextureMode();
        // TEMP ROUND34 DIAGNOSTIC - was anything actually rasterized into
        // scene_target by draw_outline()/draw_3d() just now?
        r34_dump_pixel_readback(debug_label ? debug_label : "unlabeled(cached-scene)", scene_target);

        t_scene = scene_timer.ms();          // ROUND 103
        perf::PerfTimer outline_timer;       // ROUND 103

        ray::BeginTextureMode(fxaa_target);
        ray::ClearBackground(ray::BLANK);
        ray::BeginShaderMode(outline_pass_shader);
        // (fxaa_target readback + r34_sample_x/y update happens further below,
        // after this texture is actually filled in.)
        ray::DrawTextureRec(scene_target.texture,
            {0, 0, (float)rw, -(float)rh},
            {0, 0}, ray::WHITE);
        ray::EndShaderMode();
        ray::EndTextureMode();
        // TEMP ROUND34 DIAGNOSTIC - did the outline_pass_shader composite
        // step preserve those pixels into fxaa_target (the texture actually
        // blitted to screen every frame)? Remember a good sample point from
        // it (persists on the Chara3D instance) for the post-blit backbuffer
        // probe below, which runs on every frame, not just this one.
        R34SamplePoint r34_pt = r34_dump_pixel_readback(debug_label ? debug_label : "unlabeled(cached-fxaa)", fxaa_target);
        if (r34_pt.x != -1) { r34_sample_x = r34_pt.x; r34_sample_y = r34_pt.y; }
        t_outline_pass = outline_timer.ms();   // ROUND 103
    }

    perf::PerfTimer blit_timer;   // ROUND 103
    ray::BeginShaderMode(fxaa_shader);
    ray::DrawTextureRec(fxaa_target.texture,
        {0, 0, (float)rw, -(float)rh},
        {0, 0}, ray::WHITE);
    ray::EndShaderMode();
    // TEMP ROUND34 DIAGNOSTIC - the decisive check: did that blit (the one
    // that runs on EVERY frame regardless of render_dirty, using whatever is
    // cached in fxaa_target) actually land on the real, presented backbuffer
    // at the same point r34_dump_pixel_readback found opaque chara pixels in
    // the offscreen texture?
    r34_dump_backbuffer_sample(debug_label ? debug_label : "unlabeled(post-blit)", r34_sample_x, r34_sample_y);

    const double t_blit = r103 ? blit_timer.ms() : 0.0;

    perf::PerfTimer restore_timer;   // ROUND 103
    ray::BeginBlendMode(ray::BLEND_CUSTOM_SEPARATE);
    ray::BeginMode2D(cam2d);
    const double t_restore = r103 ? restore_timer.ms() : 0.0;

    // ROUND 103: only a genuinely slow chara draw is logged, so this stays
    // silent on the ~0.4 ms/instance normal frame and fires exactly on the
    // hitches the traces keep catching.
    if (r103) {
        const double total = draw_timer.ms();
        if (total > 5.0) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "%s flush2d=%.1f scene=%.1f outline_pass=%.1f blit=%.1f "
                     "restore=%.1f",
                     debug_label ? debug_label : "?",
                     t_flush2d, t_scene, t_outline_pass, t_blit, t_restore);
            perf::note_event("chara3d_slow_draw", buf, total);
        }
    }
}

std::unique_ptr<Chara3D> make_chara_from_player_data(const PlayerData* pd, bool mirror) {
    if (pd && !pd->chara_is_costume) {
        std::string head_name = std::to_string(pd->chara_head_index);
        std::string body_name = std::to_string(pd->chara_body_index);
        return std::make_unique<Chara3D>(head_name, body_name, mirror);
    }
    std::string costume_name = pd ? std::to_string(pd->chara_cos_index) : "0";
    return std::make_unique<Chara3D>(costume_name, mirror);
}
