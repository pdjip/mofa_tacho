#ifndef MOFA_DISPLAY_ROTATION_H
#define MOFA_DISPLAY_ROTATION_H
namespace display_rotation {
bool install(); // After successful Display.begin, before UI refresh/thread start.
void reportErrors(); // Display thread only, outside the flush callback.
}
#endif
