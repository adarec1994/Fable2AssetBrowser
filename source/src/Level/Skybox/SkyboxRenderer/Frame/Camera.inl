CameraFrame BuildCameraFrame(const FlyCam& camera, int width, int height)
{
    CameraFrame frame{};
    const float cy = std::cos(camera.yaw);
    const float sy = std::sin(camera.yaw);
    const float cp = std::cos(camera.pitch);
    const float sp = std::sin(camera.pitch);
    frame.view_forward[0] = sy * cp;
    frame.view_forward[1] = sp;
    frame.view_forward[2] = cy * cp;

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        frame.view_forward[0], frame.view_forward[1],
        frame.view_forward[2], 0.0f));
    const XMVECTOR world_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(
        XMVector3Cross(world_up, forward));
    const XMVECTOR up = XMVector3Normalize(
        XMVector3Cross(forward, right));
    XMFLOAT3 right_f{};
    XMFLOAT3 up_f{};
    XMFLOAT3 forward_f{};
    XMStoreFloat3(&right_f, right);
    XMStoreFloat3(&up_f, up);
    XMStoreFloat3(&forward_f, forward);
    frame.right[0] = right_f.x;
    frame.right[1] = right_f.y;
    frame.right[2] = right_f.z;
    frame.up[0] = up_f.x;
    frame.up[1] = up_f.y;
    frame.up[2] = up_f.z;
    frame.forward[0] = forward_f.x;
    frame.forward[1] = forward_f.y;
    frame.forward[2] = forward_f.z;
    frame.fov_radians = XMConvertToRadians(60.0f);
    frame.aspect = static_cast<float>(width) / static_cast<float>(height);
    frame.tan_half_y = std::tan(frame.fov_radians * 0.5f);
    frame.tan_half_x = frame.tan_half_y * frame.aspect;
    return frame;
}
