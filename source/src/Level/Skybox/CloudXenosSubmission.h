#pragma once

#include <array>
#include <cstdint>

namespace CloudXenosSubmission {

enum class RetailExecutionBackend : std::uint8_t {
    DirectXenosCommandBuffer,
};

struct SubmissionRecipe {
    RetailExecutionBackend backend =
        RetailExecutionBackend::DirectXenosCommandBuffer;
    bool has_runtime_software_alu_evaluator = false;
    bool has_runtime_cpu_texture_sampler = false;
    std::uint32_t cloud_draw_function = 0;
    std::uint32_t select_vertex_shader_function = 0;
    std::uint32_t select_pixel_shader_function = 0;
    std::uint32_t vertex_shader_constructor = 0;
    std::uint32_t pixel_shader_constructor = 0;
    std::uint32_t vertex_fetch_cache_builder = 0;
    std::uint32_t draw_primitive_up_function = 0;
    std::uint32_t draw_state_and_packet_function = 0;
    std::uint32_t shader_state_flush_function = 0;
    std::array<std::uint32_t, 3> pixel_shader_packet_writes{};
    std::array<std::uint32_t, 6> vertex_shader_packet_writes{};
    std::uint32_t indexed_draw_packet_first_write = 0;
    std::uint32_t indexed_draw_packet_last_write = 0;
    std::uint32_t dirty_register_packet_function = 0;
    std::uint32_t driver_initialise_function = 0;
    std::array<std::uint32_t, 3> ring_buffer_initialise_calls{};
    std::uint32_t submission_function = 0;
    std::array<std::uint32_t, 2> submission_calls{};
};

const SubmissionRecipe& ExactSubmissionRecipe() noexcept;

}
