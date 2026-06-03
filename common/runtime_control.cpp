#include "runtime_control.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {
    volatile sig_atomic_t g_stop = 0;

    static void handle_signal(int) {
        g_stop = 1;
    }
}

RuntimeControl::RuntimeControl(const char* app_name)
    : app_name_(app_name) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
}

RuntimeControl::~RuntimeControl() = default;

bool RuntimeControl::stopped() const {
    return g_stop != 0;
}

bool RuntimeControl::paused() const {
    return paused_;
}

bool RuntimeControl::terminal_enabled() const {
    return terminal_.enabled();
}

void RuntimeControl::print_controls() const {
    if (terminal_enabled()) {
        std::printf("[%s] controls: S=stop  R=resume\n", app_name_);
    }
}

void RuntimeControl::poll_input() {
    if (!terminal_enabled()) {
        return;
    }

    int ch = terminal_.read_char();
    if (ch == 's' || ch == 'S') {
        if (!paused_) {
            paused_ = true;
            std::printf("[%s] paused\n", app_name_);
            std::fflush(stdout);
        }
    } else if (ch == 'r' || ch == 'R') {
        if (paused_) {
            paused_ = false;
            std::printf("[%s] resumed\n", app_name_);
            std::fflush(stdout);
        }
    }
}

void RuntimeControl::idle_while_paused() const {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
