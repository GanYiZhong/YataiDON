#include "webcam.h"
#include <SDL3/SDL_camera.h>
#include <spdlog/spdlog.h>

WebCamera::~WebCamera() {
    close();
}

WebCamera::WebCamera(WebCamera&& other) noexcept
    : m_camera(other.m_camera), m_texture(other.m_texture),
      m_width(other.m_width), m_height(other.m_height) {
    other.m_camera = nullptr;
    other.m_texture.reset();
    other.m_width  = 0;
    other.m_height = 0;
}

WebCamera& WebCamera::operator=(WebCamera&& other) noexcept {
    if (this != &other) {
        close();
        m_camera = other.m_camera;
        m_texture = other.m_texture;
        m_width  = other.m_width;
        m_height = other.m_height;
        other.m_camera = nullptr;
        other.m_texture.reset();
        other.m_width  = 0;
        other.m_height = 0;
    }
    return *this;
}

bool WebCamera::open(int device_index) {
    if (m_camera) close();

    if (!SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        spdlog::error("WebCamera: failed to init SDL camera subsystem: {}", SDL_GetError());
        return false;
    }

    int count = 0;
    SDL_CameraID* ids = SDL_GetCameras(&count);
    if (!ids || count == 0) {
        spdlog::warn("WebCamera: no camera devices found");
        SDL_free(ids);
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
        return false;
    }
    if (device_index < 0 || device_index >= count) {
        spdlog::warn("WebCamera: device index {} out of range ({} found)", device_index, count);
        SDL_free(ids);
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
        return false;
    }

    SDL_Camera* cam = SDL_OpenCamera(ids[device_index], nullptr);
    SDL_free(ids);

    if (!cam) {
        spdlog::error("WebCamera: failed to open device {}: {}", device_index, SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
        return false;
    }

    m_camera = cam;
    spdlog::info("WebCamera: opened device {}", device_index);
    return true;
}

void WebCamera::close() {
    if (m_texture.has_value()) {
        ray::UnloadTexture(m_texture.value());
        m_texture.reset();
    }
    if (m_camera) {
        SDL_CloseCamera(static_cast<SDL_Camera*>(m_camera));
        m_camera = nullptr;
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
    }
    m_width  = 0;
    m_height = 0;
}

void WebCamera::update() {
    if (!m_camera) return;

    // 0 = pending, -1 = denied
    int perm = SDL_GetCameraPermissionState(static_cast<SDL_Camera*>(m_camera));
    if (perm == -1) {
        spdlog::warn("WebCamera: permission denied");
        return;
    }
    if (perm == 0) return;

    Uint64 timestamp = 0;
    SDL_Surface* frame = SDL_AcquireCameraFrame(static_cast<SDL_Camera*>(m_camera), &timestamp);
    if (!frame) return;

    SDL_Surface* rgba = SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGBA32);
    SDL_ReleaseCameraFrame(static_cast<SDL_Camera*>(m_camera), frame);

    if (!rgba) return;

    if (rgba->pitch != rgba->w * 4) {
        spdlog::warn("WebCamera: padded surface pitch {} (w={}), skipping frame", rgba->pitch, rgba->w);
        SDL_DestroySurface(rgba);
        return;
    }

    if (!m_texture.has_value()) {
        m_width  = rgba->w;
        m_height = rgba->h;

        ray::Image img{};
        img.data    = rgba->pixels;
        img.width   = m_width;
        img.height  = m_height;
        img.mipmaps = 1;
        img.format  = ray::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

        ray::Texture2D tex = ray::LoadTextureFromImage(img);
        if (tex.id == 0) {
            spdlog::error("WebCamera: failed to create texture {}x{}", m_width, m_height);
            m_width = m_height = 0;
            SDL_DestroySurface(rgba);
            return;
        }
        ray::SetTextureFilter(tex, ray::TEXTURE_FILTER_BILINEAR);
        m_texture = tex;
    } else if (rgba->w != m_width || rgba->h != m_height) {
        // frame geometry changed - recreate the texture
        ray::UnloadTexture(m_texture.value());
        m_texture.reset();
        m_width  = rgba->w;
        m_height = rgba->h;
        ray::Image img{};
        img.data    = rgba->pixels;
        img.width   = m_width;
        img.height  = m_height;
        img.mipmaps = 1;
        img.format  = ray::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        ray::Texture2D tex = ray::LoadTextureFromImage(img);
        if (tex.id == 0) {
            spdlog::error("WebCamera: failed to create texture {}x{}", m_width, m_height);
            m_width = m_height = 0;
            SDL_DestroySurface(rgba);
            return;
        }
        ray::SetTextureFilter(tex, ray::TEXTURE_FILTER_BILINEAR);
        m_texture = tex;
    } else {
        ray::UpdateTexture(m_texture.value(), rgba->pixels);
    }

    SDL_DestroySurface(rgba);
}
