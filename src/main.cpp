#include "config.h"
#include "control/control.h"
#include "dumper/dumper.h"
#include "writer/writer.h"
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <logger/logger.h>
#include <process/process.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
    auto report_fatal_error(const std::string& title, const std::string& message) -> void {
#ifdef _WIN32
        MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
#else
        spdlog::error("{}: {}", title, message);
#endif
    }
} // namespace

auto main() -> int {
    logger::initialize();

    const auto title = std::format("{} {}", PROJECT_NAME, PROJECT_VERSION);

    spdlog::info("{} created by jonah/nopjo", title);
    spdlog::info("Github: https://github.com/nopjo/roblox-dumper\n");

#ifndef _WIN32
    if (geteuid() == 0) {
        spdlog::warn("Running as root disables macOS debugger permissions. Run without sudo.");
    }
#endif

#ifdef _WIN32
    constexpr auto k_process_name = "RobloxPlayerBeta.exe";
#else
    constexpr auto k_process_name = "RobloxPlayer";
#endif

    if (!process::g_process.attach(k_process_name)) {
        report_fatal_error(
            title,
            "Failed to attach to Roblox, please rerun the Dumper when Roblox has fully loaded.");
        return 1;
    }

    spdlog::info("Attached to Roblox. PID: {}\n", process::g_process.get_pid());

    if (!control::g_control.start(8080)) {
        report_fatal_error(title, "Failed to start control server, make sure you have no "
                                  "applications running on port 8080.");
        return 1;
    }

    spdlog::info("Control server started on port 8080");
    spdlog::info("Setup order:");
    spdlog::info("  1. Keep this terminal running (dumper must stay up on port 8080)");
    spdlog::info("  2. In another terminal: ./cloudflare_tunnel.sh");
    spdlog::info("  3. Join the Roblox Dumper game and paste the tunnel URL");
    spdlog::info("Waiting up to 120s for the game to connect via tunnel...\n");

    if (!control::g_control.wait_for_client_poll(120000)) {
        spdlog::error("No game connection detected. The tunnel cannot reach port 8080 unless "
                      "this dumper is running.");
        spdlog::error("Start ./run_dumper.sh FIRST, then cloudflare_tunnel.sh, then paste the "
                      "URL in-game.");
        report_fatal_error(title, "Game did not connect within 120 seconds. See log for setup "
                                  "order.");
        control::g_control.stop();
        return 1;
    }

    spdlog::info("Game connected (poll received). Starting dump...\n");

    const auto start_time = std::chrono::steady_clock::now();
    dumper::g_dumper.start();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    spdlog::info("Finished in {} ms ({:.2f} seconds)", elapsed.count(), elapsed.count() / 1000.0);

    dumper::writer::g_header_writer.write("offsets", elapsed);
    dumper::writer::g_json_writer.write("offsets", elapsed);
    dumper::writer::g_python_writer.write("offsets", elapsed);
    dumper::writer::g_csharp_writer.write("offsets", elapsed);

    logger::print_error_summary();

    control::g_control.stop();

    return 0;
}
