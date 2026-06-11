#include "control.h"
#include <httplib.h>
#include <random>
#include <spdlog/spdlog.h>
#include <thread>

namespace control {
    Control::~Control() { stop(false); }

    auto Control::generate_request_id() -> std::string {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        const char* hex = "0123456789abcdef";
        std::string id;
        for (int i = 0; i < 16; i++) {
            id += hex[dis(gen)];
        }
        return id;
    }

    auto Control::send_command(const json& command, int timeout_ms) -> std::optional<json> {
        if (!m_running) {
            spdlog::error("Control server not running");
            return std::nullopt;
        }

        auto request_id = generate_request_id();
        auto request = std::make_shared<PendingRequest>();
        request->request_id = request_id;

        {
            std::lock_guard<std::mutex> lock(m_requests_mutex);
            m_pending_requests[request_id] = request;
        }

        json cmd = command;
        cmd["request_id"] = request_id;

        {
            std::lock_guard<std::mutex> lock(m_command_mutex);
            m_command_queue.push(cmd);
        }

        std::unique_lock<std::mutex> lock(request->mutex);
        auto success = request->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                            [&request] { return request->completed; });

        {
            std::lock_guard<std::mutex> lock(m_requests_mutex);
            m_pending_requests.erase(request_id);
        }

        if (!success) {
            spdlog::error("Request {} timed out", request_id);
            return std::nullopt;
        }

        return request->response;
    }

    auto Control::start(int port) -> bool {
        if (m_running) {
            spdlog::warn("Control server already running");
            return false;
        }

        try {
            auto* server = new httplib::Server();
            m_server = server;

            server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
                res.set_content("OK", "text/plain");
            });

            server->Get("/poll", [this](const httplib::Request&, httplib::Response& res) {
                m_poll_count.fetch_add(1, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lock(m_command_mutex);

                if (m_command_queue.empty()) {
                    res.set_content("{}", "application/json");
                } else {
                    auto cmd = m_command_queue.front();
                    m_command_queue.pop();
                    res.set_content(cmd.dump(), "application/json");
                }
            });

            server->Post("/respond", [this](const httplib::Request& req, httplib::Response& res) {
                try {
                    auto body = json::parse(req.body);
                    auto request_id = body["request_id"].get<std::string>();

                    std::lock_guard<std::mutex> lock(m_requests_mutex);
                    auto it = m_pending_requests.find(request_id);

                    if (it != m_pending_requests.end()) {
                        std::lock_guard<std::mutex> req_lock(it->second->mutex);
                        it->second->response = body;
                        it->second->completed = true;
                        it->second->cv.notify_one();

                        res.set_content("{\"status\":\"ok\"}", "application/json");
                    } else {
                        res.status = 404;
                        res.set_content("{\"error\":\"request_id not found\"}", "application/json");
                    }
                } catch (const std::exception& e) {
                    res.status = 400;
                    res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                }
            });

            m_server_thread = std::thread([this, server, port]() {
                m_running = true;
                server->listen("127.0.0.1", port);
                m_running = false;
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return m_running;
        } catch (const std::exception& e) {
            spdlog::error("Failed to start control server: {}", e.what());
            return false;
        }
    }

    auto Control::wait_for_client_poll(int timeout_ms) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            if (m_poll_count > 0) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        return false;
    }

    auto Control::stop(bool log_stop) -> void {
        if (!m_server) {
            return;
        }

        auto* server = static_cast<httplib::Server*>(m_server);
        server->stop();

        if (m_server_thread.joinable()) {
            m_server_thread.join();
        }

        delete server;
        m_server = nullptr;
        m_running = false;

        if (log_stop) {
            spdlog::info("Control server stopped");
        }
    }
} // namespace control
