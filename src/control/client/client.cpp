#include "client.h"
#include "control/control.h"
#include <spdlog/spdlog.h>

namespace control::client {

    Client::Client(const std::string& base_url) : m_base_url(base_url) {}

    auto Client::send_request(const std::string& endpoint, const json& payload)
        -> std::optional<json> {
        return g_control.send_command(payload);
    }

    template <typename T>
    auto Client::execute(const std::string& action, int timeout_ms) -> std::optional<T> {
        json request = {{"action", action}};
        auto response = g_control.send_command(request, timeout_ms);

        if (!response) {
            return std::nullopt;
        }

        try {
            return response->get<T>();
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse {}: {}", action, e.what());
            return std::nullopt;
        }
    }

    auto Client::get_data_model_information(int timeout_ms) -> std::optional<DataModelInfo> {
        return execute<DataModelInfo>("get_data_model_information", timeout_ms);
    }

    auto Client::get_workspace_information() -> std::optional<WorkspaceInfo> {
        return execute<WorkspaceInfo>("get_workspace_information");
    }

    auto Client::get_player_information() -> std::optional<PlayerInfo> {
        return execute<PlayerInfo>("get_player_information");
    }

    auto Client::get_part_properties() -> std::optional<PartPropertiesInfo> {
        return execute<PartPropertiesInfo>("get_part_properties");
    }

    auto Client::set_part_properties() -> std::optional<PartPropertiesInfo> {
        return execute<PartPropertiesInfo>("set_part_properties");
    }

    auto Client::get_humanoid_properties() -> std::optional<HumanoidPropertiesInfo> {
        return execute<HumanoidPropertiesInfo>("get_humanoid_properties");
    }

    auto Client::get_viewport_size(int timeout_ms) -> std::optional<ViewportInfo> {
        return execute<ViewportInfo>("get_viewport_size", timeout_ms);
    }

    auto Client::get_frame_properties() -> std::optional<FramePropertiesInfo> {
        return execute<FramePropertiesInfo>("get_frame_properties");
    }

    auto Client::get_frame_absolutes() -> std::optional<FrameAbsolutesInfo> {
        return execute<FrameAbsolutesInfo>("get_frame_absolutes");
    }

    auto Client::get_character_mesh_properties() -> std::optional<CharacterMeshPropertiesInfo> {
        return execute<CharacterMeshPropertiesInfo>("get_character_mesh_properties");
    }

    auto Client::get_tool_properties() -> std::optional<ToolPropertiesInfo> {
        return execute<ToolPropertiesInfo>("get_tool_properties");
    }

    auto Client::get_proximity_prompt_properties() -> std::optional<ProximityPromptPropertiesInfo> {
        return execute<ProximityPromptPropertiesInfo>("get_proximity_prompt_properties");
    }

    auto Client::get_textlabel_properties() -> std::optional<TextLabelPropertiesInfo> {
        return execute<TextLabelPropertiesInfo>("get_textlabel_properties");
    }

    auto Client::get_textbutton_properties() -> std::optional<TextButtonPropertiesInfo> {
        return execute<TextButtonPropertiesInfo>("get_textbutton_properties");
    }

} // namespace control::client