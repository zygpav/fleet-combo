#pragma once
// Minimal cooperative fibers: each game runs its own loop on its own stack, on the main thread.
// Switching on the main thread keeps SDL, the GL/D3D context and the window owned by one thread.

#include <cstddef>
#include <functional>

namespace FleetHost {

class Fiber {
  public:
    // Turns the calling thread into the scheduler fiber. Call once, on the main thread, first.
    static void InitMainFiber();
    // The scheduler fiber created by InitMainFiber.
    static Fiber* Main();

    Fiber(std::function<void()> entry, size_t stackSize);
    ~Fiber();
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    // Suspends the current fiber and continues `target` where it last stopped (or at its entry).
    static void SwitchTo(Fiber* target);

    bool Finished() const {
        return mFinished;
    }

    // Internal: entry point every fiber starts in.
    static void Trampoline(Fiber* self);

  private:
    Fiber() = default; // the main fiber

    std::function<void()> mEntry;
    bool mFinished = false;
    void* mHandle = nullptr; // Windows: fiber handle; elsewhere: ucontext_t*
    void* mStack = nullptr;
    size_t mStackSize = 0;
};

} // namespace FleetHost
