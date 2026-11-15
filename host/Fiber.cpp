#include "Fiber.h"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif

namespace FleetHost {

namespace {
Fiber* sMain = nullptr;
Fiber* sCurrent = nullptr;
} // namespace

#ifdef _WIN32

void Fiber::InitMainFiber() {
    if (sMain != nullptr) {
        return;
    }
    sMain = new Fiber();
    sMain->mHandle = ConvertThreadToFiber(nullptr);
    if (sMain->mHandle == nullptr) {
        throw std::runtime_error("ConvertThreadToFiber failed");
    }
    sCurrent = sMain;
}

static VOID CALLBACK FiberProc(LPVOID parameter) {
    Fiber::Trampoline(static_cast<Fiber*>(parameter));
}

Fiber::Fiber(std::function<void()> entry, size_t stackSize) : mEntry(std::move(entry)), mStackSize(stackSize) {
    // Reserve a large stack (the games were written for a big main-thread stack), commit lazily.
    mHandle = CreateFiberEx(0, stackSize, FIBER_FLAG_FLOAT_SWITCH, FiberProc, this);
    if (mHandle == nullptr) {
        throw std::runtime_error("CreateFiberEx failed");
    }
}

Fiber::~Fiber() {
    if (this != sMain && mHandle != nullptr) {
        DeleteFiber(mHandle);
    }
}

void Fiber::SwitchTo(Fiber* target) {
    if (target == sCurrent) {
        return;
    }
    sCurrent = target;
    SwitchToFiber(target->mHandle);
}

#else

void Fiber::InitMainFiber() {
    if (sMain != nullptr) {
        return;
    }
    sMain = new Fiber();
    sMain->mHandle = new ucontext_t();
    sCurrent = sMain;
}

static void UcontextEntry(unsigned int high, unsigned int low) {
    uintptr_t pointer = (static_cast<uintptr_t>(high) << 32) | static_cast<uintptr_t>(low);
    Fiber::Trampoline(reinterpret_cast<Fiber*>(pointer));
}

Fiber::Fiber(std::function<void()> entry, size_t stackSize) : mEntry(std::move(entry)), mStackSize(stackSize) {
    auto* context = new ucontext_t();
    getcontext(context);
    mStack = std::malloc(stackSize);
    if (mStack == nullptr) {
        throw std::runtime_error("fiber stack allocation failed");
    }
    context->uc_stack.ss_sp = mStack;
    context->uc_stack.ss_size = stackSize;
    context->uc_link = static_cast<ucontext_t*>(sMain->mHandle);
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(this);
    makecontext(context, reinterpret_cast<void (*)()>(UcontextEntry), 2, static_cast<unsigned int>(pointer >> 32),
                static_cast<unsigned int>(pointer & 0xFFFFFFFFu));
    mHandle = context;
}

Fiber::~Fiber() {
    delete static_cast<ucontext_t*>(mHandle);
    std::free(mStack);
}

void Fiber::SwitchTo(Fiber* target) {
    if (target == sCurrent) {
        return;
    }
    Fiber* previous = sCurrent;
    sCurrent = target;
    swapcontext(static_cast<ucontext_t*>(previous->mHandle), static_cast<ucontext_t*>(target->mHandle));
}

#endif

Fiber* Fiber::Main() {
    return sMain;
}

void Fiber::Trampoline(Fiber* self) {
    self->mEntry();
    self->mFinished = true;
    // A finished fiber must never return on Windows (that exits the thread): hand control back for good.
    SwitchTo(sMain);
}

} // namespace FleetHost
