/*
 * =============================================================================
 * ui_system.h  —  Neon Table Tennis  |  UI Rendering & Interactive Elements
 * =============================================================================
 *
 * PURPOSE
 * -------
 * Declares the user interface subsystem for Neon Table Tennis. Provides styled
 * neon glassmorphic buttons with smooth hover animations, floating sinusoidal
 * title banners, and glowing rounded backing panels.
 *
 * DESIGN PHILOSOPHY
 * -----------------
 * The UI operates in a hybrid immediate-mode style:
 *   - Geometry and state (such as hover animation transitions) are stored in
 *     lightweight structs (`UIButton`).
 *   - Drawing functions compute dynamic scaling, glow colors, and bounds expansion
 *     in-place per frame.
 *   - Strict two-phase architecture: EngineUpdate handles input hits, while
 *     EngineDraw calls UIButtonDraw() to animate and render.
 *
 * ANIMATION MATHEMATICS
 * ---------------------
 *   1. Button Hover Smoothing:
 *      Exponential approach toward target hover state [0, 1]:
 *        b->hover += ((isHovered ? 1.0f : 0.0f) - b->hover) * min(1.0f, dt * 12.0f);
 *      Provides rapid, natural responsive easing without overshoot.
 *
 *   2. Button Expansion (Micro-Scale):
 *      scale = 1.0f + b->hover * 0.025f;  (scales outward by up to 2.5%)
 *      Rectangle expands symmetrically about its center:
 *        new_x = x - (w * (scale - 1) * 0.5)
 *        new_y = y - (h * (scale - 1) * 0.5)
 *
 *   3. Sinusoidal Floating Title:
 *      y_offset = sin(time * 1.2f) * 4.0f;
 *      Subtle ambient vertical floating for retro neon sign feel.
 *
 * =============================================================================
 */

#ifndef NTT_UI_SYSTEM_H
#define NTT_UI_SYSTEM_H

#include "raylib.h"
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * UIButton
 * ---------------------------------------------------------------------------
 * Encapsulates the configuration and animation state of an interactive button.
 * --------------------------------------------------------------------------- */
typedef struct UIButton {
    Rectangle   bounds;     /* Outer bounding box of the button (x, y, w, h)  */
    const char *label;      /* Text label displayed in the center of button   */
    float       hover;      /* Hover animation progress in range [0.0f, 1.0f] */
    bool        enabled;    /* Whether the button accepts input and lights up */
} UIButton;

/* ---------------------------------------------------------------------------
 * UI Drawing & Interaction API
 * --------------------------------------------------------------------------- */

/*
 * UIButtonDraw
 * ------------
 * Updates the button's internal hover animation and renders its glassmorphic
 * neon rounded rectangle, border glow, and centered text label.
 *
 * @param b      Pointer to the button struct to update and draw.
 * @param mouse  Current mouse cursor position in screen coordinates.
 * @param dt     Delta time since previous frame in seconds (for animation).
 * @return       true if the button was hovered and released on this frame.
 */
bool UIButtonDraw(UIButton *b, Vector2 mouse, float dt);

/*
 * UIDrawTitle
 * -----------
 * Renders the primary arcade title and subtitle header with an ambient
 * sinusoidal vertical float motion.
 *
 * @param title     Primary large headline string (cyan glow).
 * @param subtitle  Secondary descriptive tagline string.
 * @param time      Accumulated global animation clock in seconds.
 */
void UIDrawTitle(const char *title, const char *subtitle, float time);

/*
 * UIDrawPanel
 * -----------
 * Renders a dark glassmorphic rounded rectangular panel with a glowing colored
 * outline border for HUD overlays and modal backdrops.
 *
 * @param r     Screen-space rectangle bounds.
 * @param tint  Color applied to the glowing border outline.
 */
void UIDrawPanel(Rectangle r, Color tint);

#endif /* NTT_UI_SYSTEM_H */
