// Stub implementations for Xbox 360 imports not provided by the SDK.
// 'Splosion Man uses the USB camera API (XUsbcam*) which isn't implemented.
// These are no-ops — the game will work without camera support.

#include <rex/ppc.h>

// XUsbcamCreate - create USB camera handle
extern "C" void __imp__XUsbcamCreate(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;  // NULL handle (no camera)
}

// XUsbcamDestroy - destroy USB camera handle
extern "C" void __imp__XUsbcamDestroy(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}

// XUsbcamGetState - get camera state
extern "C" void __imp__XUsbcamGetState(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}

// XUsbcamReadFrame - read camera frame
extern "C" void __imp__XUsbcamReadFrame(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}

// XUsbcamSetCaptureMode - set capture mode
extern "C" void __imp__XUsbcamSetCaptureMode(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}

// XUsbcamSetConfig - configure USB camera
extern "C" void __imp__XUsbcamSetConfig(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}

// XUsbcamSetView - set camera view
extern "C" void __imp__XUsbcamSetView(PPCContext& ctx, uint8_t* base) {
    ctx.r3.u64 = 0;
}
