void FlyCam_Reset(FlyCam& cam, float cx, float cy, float cz, float radius) {
    cam.pos[0] = cx;
    cam.pos[1] = cy;
    cam.pos[2] = cz + radius * 3.0f;
    cam.yaw = 3.14159265f;
    cam.pitch = 0.0f;
    cam.move_speed = radius * 2.0f;
    cam.is_looking = false;
}
void FlyCam_Update(FlyCam& cam, float dt, bool w, bool s, bool a, bool d, bool q, bool e, float mouse_dx, float mouse_dy) {
    if (cam.is_looking) {
        const float sx = S.cam_invert_x ? -1.0f : 1.0f;
        const float sy = S.cam_invert_y ? -1.0f : 1.0f;
        cam.yaw   -= sx * mouse_dx * cam.look_sensitivity;
        cam.pitch += sy * mouse_dy * cam.look_sensitivity;
        const float max_pitch = 1.5f;
        if (cam.pitch > max_pitch) cam.pitch = max_pitch;
        if (cam.pitch < -max_pitch) cam.pitch = -max_pitch;
    }
    float cy = cosf(cam.yaw);
    float sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch);
    float sp = sinf(cam.pitch);
    float forward[3] = { sy * cp, sp, cy * cp };
    float right[3] = { cy, 0.0f, -sy };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    float speed = cam.move_speed * dt;
    if (w) {
        cam.pos[0] += forward[0] * speed;
        cam.pos[1] += forward[1] * speed;
        cam.pos[2] += forward[2] * speed;
    }
    if (s) {
        cam.pos[0] -= forward[0] * speed;
        cam.pos[1] -= forward[1] * speed;
        cam.pos[2] -= forward[2] * speed;
    }
    if (a) {
        cam.pos[0] += right[0] * speed;
        cam.pos[1] += right[1] * speed;
        cam.pos[2] += right[2] * speed;
    }
    if (d) {
        cam.pos[0] -= right[0] * speed;
        cam.pos[1] -= right[1] * speed;
        cam.pos[2] -= right[2] * speed;
    }
    if (e) {
        cam.pos[1] += speed;
    }
    if (q) {
        cam.pos[1] -= speed;
    }
}
