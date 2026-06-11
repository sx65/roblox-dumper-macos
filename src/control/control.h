#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace control {
    using json = nlohmann::json;

    struct PendingRequest {
        std::string request_id;
        json response;
        bool completed = false;
        std::mutex mutex;
        std::condition_variable cv;
    };

    class Control {
      public:
        Control() = default;
        ~Control();

        auto start(int port = 8080) -> bool;
        auto stop(bool log_stop = true) -> void;
        auto is_running() const -> bool { return m_running; }

        auto send_command(const json& command, int timeout_ms = 5000) -> std::optional<json>;
        auto wait_for_client_poll(int timeout_ms = 120000) -> bool;
        auto has_client_polled() const -> bool { return m_poll_count > 0; }

      private:
        auto generate_request_id() -> std::string;

        bool m_running = false;
        std::atomic<uint64_t> m_poll_count{0};
        void* m_server = nullptr;
        std::thread m_server_thread;
        std::unordered_map<std::string, std::shared_ptr<PendingRequest>> m_pending_requests;
        std::mutex m_requests_mutex;
        std::queue<json> m_command_queue;
        std::mutex m_command_mutex;
    };

    inline Control g_control;
} // namespace control