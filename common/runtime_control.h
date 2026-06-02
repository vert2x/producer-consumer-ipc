#pragma once

#include <csignal>

class RuntimeControl {
public:
    explicit RuntimeControl(const char* app_name);
    ~RuntimeControl();

    bool stopped() const;
    bool paused() const;
    bool terminal_enabled() const;

    void print_controls() const;
    void poll_input();
    void idle_while_paused() const;

private:
    const char* app_name_;
    bool paused_ = false;
};
