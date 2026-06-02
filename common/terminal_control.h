#pragma once

#include <cstdio>
#include <termios.h>
#include <unistd.h>

class TerminalControl {
public:
    TerminalControl() {
        enabled_ = isatty(STDIN_FILENO);
        if (!enabled_) {
            return;
        }

        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            enabled_ = false;
            return;
        }

        termios raw = original_;
        raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            enabled_ = false;
            return;
        }
    }

    ~TerminalControl() {
        if (enabled_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
    }

    bool enabled() const {
        return enabled_;
    }

    int read_char() const {
        if (!enabled_) {
            return -1;
        }

        unsigned char ch = 0;
        ssize_t n = ::read(STDIN_FILENO, &ch, 1);
        return n == 1 ? ch : -1;
    }

private:
    bool enabled_ = false;
    termios original_{};
};
