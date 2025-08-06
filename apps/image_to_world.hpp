// apps/real_world_overlay/image_to_world.hpp
#pragma once
#include <utility>

inline std::pair<float, float> imageToWorld(float px, float py,
                                            int img_width, int img_height,
                                            float fov_x_m, float fov_y_m,
                                            float cam_x_m, float cam_y_m) {
    float norm_x = (px / img_width) - 0.5f;
    float norm_y = (py / img_height) - 0.5f;

    float dx = norm_x * fov_x_m;
    float dy = norm_y * fov_y_m;

    float world_x = cam_x_m + dx;
    float world_y = cam_y_m + dy;

    return {world_x, world_y};
}
