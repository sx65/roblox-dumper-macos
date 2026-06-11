#include "visual_engine.h"
#include "control/client/client.h"
#include "dumper/dumper.h"
#include "dumper/macros.h"
#include "process/helpers/helpers.h"
#include "process/memory/memory.h"
#include "process/rtti/rtti.h"
#include <array>
#include <spdlog/spdlog.h>
#include <vector>

namespace dumper::stages::visual_engine {

    static auto get_pointers() -> bool {
        const auto results = process::helpers::find_pointer_by_rtti(
            ".data", {"VisualEngine@Graphics@RBX", "DataModel@RBX"});

        if (!results.at("VisualEngine@Graphics@RBX")) {
            spdlog::error("Failed to find VisualEngine pointer");
            return false;
        }

        if (!results.at("DataModel@RBX")) {
            spdlog::error("Failed to find DataModel pointer");
            return false;
        }

        g_dumper.add_offset("VisualEngine", "Pointer", *results.at("VisualEngine@Graphics@RBX"));
        g_dumper.add_offset("FakeDataModel", "Pointer", *results.at("DataModel@RBX"));

        return true;
    }

    static auto dump_render_view(const uintptr_t visual_engine) -> bool {
        const auto render_view_offset =
            process::Rtti::find(visual_engine, "RenderView@Graphics@RBX");

        if (!render_view_offset) {
            spdlog::error("Failed to get RenderView offset from VisualEngine");
            return false;
        }

        g_dumper.add_offset("VisualEngine", "RenderView", *render_view_offset);

        const auto render_view =
            process::Memory::read<uintptr_t>(visual_engine + *render_view_offset);

        FIND_AND_ADD_OFFSET(*render_view, RenderView, uint16_t, LightingValid, 257, 0x300, 0x2);
        g_dumper.add_offset("RenderView", "SkyboxValid", 0x28d); // hardcoded for now

        return true;
    };

    static auto dump_view_matrix(const uintptr_t visual_engine) -> bool {
        auto is_valid_view_matrix = [](const float mat[16]) -> bool {
            if (std::abs(mat[11] - 0.1f) > 0.01f)
                return false;

            if (std::abs(mat[14] + 1.0f) < 0.01f && std::abs(mat[15]) < 0.01f) {
                return false; // thiss is projection matrix
            }

            if (std::abs(mat[15]) < 10.0f || std::abs(mat[15]) > 10000.0f) {
                return false;
            }

            for (int i = 0; i < 16; i++) {
                if (std::isnan(mat[i]) || std::isinf(mat[i])) {
                    return false;
                }
            }

            return true;
        };

        for (size_t offset = 0; offset < 0x2000; offset += 0x10) {
            float mat[16];
            bool valid_read = true;

            for (int i = 0; i < 16; i++) {
                auto val = process::Memory::read<float>(visual_engine + offset + (i * 4));
                if (!val) {
                    valid_read = false;
                    break;
                }
                mat[i] = *val;
            }

            if (!valid_read)
                continue;

            if (is_valid_view_matrix(mat)) {
                g_dumper.add_offset("VisualEngine", "ViewMatrix", offset);
                return true;
            }
        }

        spdlog::error("Failed to find ViewMatrix");
        return false;
    }

    auto dump() -> bool {
        if (!get_pointers()) {
            return false;
        }

        const auto visual_engine = process::Memory::read<uintptr_t>(
            process::g_process.get_module_base() + *g_dumper.get_offset("VisualEngine", "Pointer"));

        dump_render_view(*visual_engine);
        dump_view_matrix(*visual_engine);

        std::vector<glm::vec2> dimension_candidates;

        if (const auto window_dimensions = process::g_process.get_window_dimensions()) {
            spdlog::info("Window dimensions (pixels): {}x{}", window_dimensions->x,
                         window_dimensions->y);
            dimension_candidates.push_back(*window_dimensions);

            const glm::vec2 points_dims(window_dimensions->x / 2.0f, window_dimensions->y / 2.0f);
            if (points_dims.x > 0 && points_dims.y > 0) {
                dimension_candidates.push_back(points_dims);
            }
        }

        auto dimensions_offset =
            process::helpers::find_screen_dimensions_offset(*visual_engine, dimension_candidates);

        if (!dimensions_offset) {
            if (const auto viewport = control::client::g_client.get_viewport_size(500)) {
                spdlog::info("Using in-game viewport size: {}x{}", viewport->viewport_width,
                             viewport->viewport_height);
                dimension_candidates.emplace_back(viewport->viewport_width,
                                                  viewport->viewport_height);
                dimensions_offset = process::helpers::find_screen_dimensions_offset(
                    *visual_engine, dimension_candidates);
            }
        }

        if (!dimensions_offset) {
            spdlog::error("Failed to get Dimensions offset from VisualEngine");
            return false;
        }

        spdlog::info("Found VisualEngine::Dimensions at offset 0x{:X}", *dimensions_offset);

        g_dumper.add_offset("VisualEngine", "Dimensions", *dimensions_offset);

        const auto fake_data_model = process::Rtti::find(*visual_engine, "DataModel@RBX");

        if (!fake_data_model) {
            spdlog::error("Failed to get FakeDataModel offset from VisualEngine");
            return false;
        }

        g_visual_engine = *visual_engine;

        g_dumper.add_offset("VisualEngine", "FakeDataModel", *fake_data_model);

        return true;
    }
} // namespace dumper::stages::visual_engine
