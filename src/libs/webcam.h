#pragma once
#include <SDL3/SDL.h>
#include "ray.h"
#include <optional>

class WebCamera {
public:
    WebCamera() = default;
    ~WebCamera();

    WebCamera(const WebCamera&) = delete;
    WebCamera& operator=(const WebCamera&) = delete;
    WebCamera(WebCamera&& other) noexcept;
    WebCamera& operator=(WebCamera&& other) noexcept;

    bool open(int device_index = 0);
    void close();
    void update();

    bool is_open()  const { return m_camera != nullptr; }
    bool is_ready() const { return m_texture.has_value(); }
    const ray::Texture2D& get_texture() const { return m_texture.value(); }
    int width()  const { return m_width; }
    int height() const { return m_height; }

private:
    SDL_Camera* m_camera = nullptr;
    std::optional<ray::Texture2D> m_texture;
    int m_width  = 0;
    int m_height = 0;
};
