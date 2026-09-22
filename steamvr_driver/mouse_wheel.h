// Global mouse wheel reader.
//
// The wheel is the natural control for reaching the hand in and out, but
// unlike buttons it carries a delta rather than a state, so GetAsyncKeyState
// cannot see it. Raw Input can, and with RIDEV_INPUTSINK it keeps delivering
// while some other window has focus, which is what a driver inside
// vrserver.exe needs.
//
// Raw Input is used rather than a WH_MOUSE_LL hook on purpose: a low-level
// hook sits in the input path of every process on the machine, where this
// only listens.
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace dk2vr {

class MouseWheelReader {
public:
    MouseWheelReader() = default;
    ~MouseWheelReader();

    MouseWheelReader(const MouseWheelReader&) = delete;
    MouseWheelReader& operator=(const MouseWheelReader&) = delete;

    // Idempotent; the listener thread and its message-only window are created
    // on the first call.
    void start();
    void stop();

    bool isRunning() const noexcept { return running_; }

    // Notches scrolled since the last call, positive for scroll-up. Reading
    // clears the accumulator, so no motion is counted twice.
    double consume();

private:
    void threadMain();

    std::thread thread_;
    std::atomic<bool> running_ {false};
    // Accumulated in WHEEL_DELTA units so partial notches from a free-spinning
    // wheel are not thrown away.
    std::atomic<std::int64_t> accumulated_ {0};
    std::atomic<std::uint32_t> threadId_ {0};
};

} // namespace dk2vr
