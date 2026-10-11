/**
 ******************************************************************************
 * @file        core/fiber_android.cpp
 * @brief       Android backend for rex::thread::Fiber.
 *
 * Bionic provides the ucontext_t type but none of the ucontext functions
 * (getcontext/makecontext/swapcontext) on 64-bit, so the POSIX backend
 * cannot work on Android. This backend switches fibers with a small
 * hand-rolled AArch64 context swap: the callee-saved general registers
 * (x19-x30), the stack pointer, and the callee-saved NEON registers
 * (d8-d15) - everything the AAPCS requires a call to preserve, which is
 * exactly the state a cooperative fiber switch must keep.
 *
 * Context layout in Fiber::ctx_ (uint64_t slots):
 *   0..9   x19-x28
 *   10     x29 (frame pointer)
 *   11     x30 (continuation address)
 *   12     SP
 *   13..20 d8-d15
 ******************************************************************************
 */

#include <rex/platform.h>

#if REX_PLATFORM_ANDROID

#include <rex/thread/fiber.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>

extern "C" void TpFiberSwitch(uint64_t* from, const uint64_t* to);

extern "C" __attribute__((naked)) void TpFiberSwitch(uint64_t* from,
                                                     const uint64_t* to) {
  __asm__ volatile(
      // Save the current fiber's callee-saved state into `from` (x0).
      "stp x19, x20, [x0, #0]\n"
      "stp x21, x22, [x0, #16]\n"
      "stp x23, x24, [x0, #32]\n"
      "stp x25, x26, [x0, #48]\n"
      "stp x27, x28, [x0, #64]\n"
      "stp x29, x30, [x0, #80]\n"
      "mov x16, sp\n"
      "str x16, [x0, #96]\n"
      "stp d8, d9, [x0, #104]\n"
      "stp d10, d11, [x0, #120]\n"
      "stp d12, d13, [x0, #136]\n"
      "stp d14, d15, [x0, #152]\n"
      // Restore the target fiber's state from `to` (x1) and resume it.
      "ldp x19, x20, [x1, #0]\n"
      "ldp x21, x22, [x1, #16]\n"
      "ldp x23, x24, [x1, #32]\n"
      "ldp x25, x26, [x1, #48]\n"
      "ldp x27, x28, [x1, #64]\n"
      "ldp x29, x30, [x1, #80]\n"
      "ldr x16, [x1, #96]\n"
      "mov sp, x16\n"
      "ldp d8, d9, [x1, #104]\n"
      "ldp d10, d11, [x1, #120]\n"
      "ldp d12, d13, [x1, #136]\n"
      "ldp d14, d15, [x1, #152]\n"
      "ret\n");
}

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  f->stack_.resize(stack_size);

  // Fresh context: stack pointer at the (16-byte aligned) top of the new
  // stack, continuation at the trampoline. All other slots stay zeroed;
  // the trampoline reads entry_/arg_ from tls_current_, which SwitchTo
  // publishes before the first switch.
  uintptr_t top =
      reinterpret_cast<uintptr_t>(f->stack_.data()) + f->stack_.size();
  top &= ~uintptr_t(0xF);
  f->ctx_[12] = top;
  f->ctx_[11] = reinterpret_cast<uint64_t>(&Fiber::Trampoline);
  return f;
}

/*static*/ void Fiber::Trampoline() {
  // tls_current_ was updated by SwitchTo before the context switch landed
  // here.
  Fiber* f = tls_current_;
  f->entry_(f->arg_);
  // Guest fiber entries are not expected to return. (The POSIX backend
  // would follow uc_link == nullptr here and end the thread.)
  std::abort();
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;
  tls_current_ = target;
  TpFiberSwitch(from->ctx_, target->ctx_);
}

void Fiber::Destroy() {
  // Thread fibers are destroyed from the owning thread itself.
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ &&
           "Destroy called on the currently running fiber");
  }
  delete this;
}

}  // namespace rex::thread

#endif  // REX_PLATFORM_ANDROID
