/** @file display_rotation.h
 * Display-thread-only adapter. Install after Display.begin; report outside flush. Successful DMA status is not evidence of visible panel output.
 *
 * Inputs are the initialized default LVGL display and later display-thread report calls; output is callback registration or status. This interface does not transfer framebuffer ownership or authorize concurrent LVGL access.
 */
#ifndef MOFA_DISPLAY_ROTATION_H
#define MOFA_DISPLAY_ROTATION_H
namespace display_rotation {
/** Register the project flush callback on the default LVGL display. False if none exists. */
bool install(); // After successful Display.begin, before UI refresh/thread start.
/** Emit bounded diagnostics outside flush; error counts persist when output is disabled. */
void reportErrors(); // Display thread only, outside the flush callback.
}
#endif
