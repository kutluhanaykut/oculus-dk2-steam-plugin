#include "mouse_wheel.h"

#include "driverlog.h"

#include <Windows.h>

#include <vector>

namespace dk2vr {
namespace {

constexpr wchar_t kWindowClassName[] = L"Dk2VrMouseWheelSink";

LRESULT CALLBACK wheelWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INPUT) {
        auto* accumulator = reinterpret_cast<std::atomic<std::int64_t>*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (accumulator != nullptr) {
            UINT size = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                sizeof(RAWINPUTHEADER));
            if (size > 0 && size < 1024) {
                std::vector<BYTE> buffer(size);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                        buffer.data(), &size, sizeof(RAWINPUTHEADER))
                    == size) {
                    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
                    if (raw->header.dwType == RIM_TYPEMOUSE
                        && (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
                        // usButtonData carries a signed delta in WHEEL_DELTA units.
                        const auto delta =
                            static_cast<short>(raw->data.mouse.usButtonData);
                        accumulator->fetch_add(static_cast<std::int64_t>(delta));
                    }
                }
            }
        }
        // Let DefWindowProc clean up after the message.
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

MouseWheelReader::~MouseWheelReader()
{
    stop();
}

void MouseWheelReader::start()
{
    if (running_) {
        return;
    }
    running_ = true;
    thread_ = std::thread(&MouseWheelReader::threadMain, this);
}

void MouseWheelReader::stop()
{
    if (!running_) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    running_ = false;
    // The thread is parked in GetMessage, so it needs a message to wake up.
    const auto id = threadId_.load();
    if (id != 0) {
        PostThreadMessageW(static_cast<DWORD>(id), WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MouseWheelReader::threadMain()
{
    threadId_ = static_cast<std::uint32_t>(GetCurrentThreadId());

    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = wheelWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClassName;
    // A second registration is harmless; the driver may be reloaded in the
    // same process across a SteamVR restart.
    RegisterClassExW(&windowClass);

    // HWND_MESSAGE creates a message-only window: never shown, never in the
    // taskbar, but able to receive WM_INPUT.
    HWND window = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    if (window == nullptr) {
        DriverLog("DK2 input: could not create the mouse wheel sink window (error %lu); "
                  "wheel control will be unavailable.",
            GetLastError());
        running_ = false;
        return;
    }

    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&accumulated_));

    RAWINPUTDEVICE device {};
    device.usUsagePage = 0x01; // generic desktop
    device.usUsage = 0x02;     // mouse
    // INPUTSINK keeps the deltas coming while another window has focus.
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = window;
    if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE) {
        DriverLog("DK2 input: RegisterRawInputDevices failed (error %lu); wheel "
                  "control will be unavailable.",
            GetLastError());
        DestroyWindow(window);
        running_ = false;
        return;
    }

    DriverLog("DK2 input: mouse wheel listener started.");

    MSG message {};
    while (running_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) { // WM_QUIT or error
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    // Unregister before the window goes away, or Windows keeps delivering to
    // a dead handle.
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));

    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    DestroyWindow(window);
    threadId_ = 0;
    DriverLog("DK2 input: mouse wheel listener stopped.");
}

double MouseWheelReader::consume()
{
    const std::int64_t ticks = accumulated_.exchange(0);
    return static_cast<double>(ticks) / static_cast<double>(WHEEL_DELTA);
}

} // namespace dk2vr
