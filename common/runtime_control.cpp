#include "runtime_control.h"

#include <cstdio>
#include <unistd.h>

#include "terminal_control.h"

namespace {
volatile sig_atomic_t g_stop = 0;
TerminalControl* g_terminal = nullptr;

void handle_signal(int) {
    g_stop = 1;
}
}  // namespace

RuntimeControl::RuntimeControl(const char* app_name)
    : app_name_(app_name) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    static TerminalControl terminal;
    g_terminal = &terminal;
}

RuntimeControl::~RuntimeControl() = default;

bool RuntimeControl::stopped() const {
    return g_stop != 0;
}

bool RuntimeControl::paused() const {
    return paused_;
}

bool RuntimeControl::terminal_enabled() const {
    return g_terminal != nullptr && g_terminal->enabled();
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

    int ch = g_terminal->read_char();
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
    usleep(10000);
}
