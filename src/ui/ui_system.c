/*
 * =============================================================================
 * ui_system.c  —  Neon Table Tennis  |  UI Rendering & Interactive Elements
 * =============================================================================
 *
 * IMPLEMENTATION DETAILS
 * ----------------------
 * Provides rendering routines for immediate/retained hybrid UI widgets with
 * glassmorphism, glowing rounded outlines, and dynamic hover animations.
 *
 * MATHEMATICAL ALGORITHMS
 * -----------------------
 * 1. Exponential Smoothing (Hover Lerp):
 *    Target hover is 1.0f when mouse is inside bounding box, 0.0f otherwise.
 *    Rather than a linear ramp, we use exponential smoothing:
 *      b->hover += (target - b->hover) * min(1.0f, dt * 12.0f);
 *    This produces smooth acceleration and deceleration without needing complex
 *    curve evaluations.
 *
 * 2. Symmetric Center-Anchored Expansion:
 *    When hovered, the button expands by up to 2.5%:
 *      scale = 1.0f + hover * 0.025f;
 *      w' = w * scale;
 *      h' = h * scale;
 *      x' = x - (w' - w) * 0.5f;
 *      y' = y - (h' - h) * 0.5f;
 *    This ensures the expansion is centered and does not drift towards bottom-right.
 *
 * 3. Title Floating Sine Wave:
 *    y(t) = y_base + sin(t * 1.2f) * 4.0f;
 *    Produces a gentle 4-pixel vertical oscillation at ~0.19 Hz frequency.
 *
 * =============================================================================
 */

#include "ui_system.h"
#include "math_3d.h"
#include <stddef.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * Panel Rendering
 * --------------------------------------------------------------------------- */

/*
 * UIDrawPanel
 * -----------
 * Renders a semi-transparent dark navy rounded rectangle with a colored neon border.
 *
 * @param r     Screen-space bounding box.
 * @param tint  Border accent color.
 */
void UIDrawPanel(Rectangle r, Color tint)
{
    /* Background: translucent dark cyber-navy fill */
    Color bg = (Color){ 8, 15, 35, 225 };
    DrawRectangleRounded(r, 0.12f, 12, bg);

    /* Border: 2-pixel glowing outline */
    DrawRectangleRoundedLinesEx(r, 0.12f, 12, 2.0f, tint);
}

/* ---------------------------------------------------------------------------
 * Button Rendering & Interaction
 * --------------------------------------------------------------------------- */

/*
 * UIButtonDraw
 * ------------
 * Evaluates hover state, updates animated parameters, and renders the button.
 *
 * @param b      Pointer to UIButton state.
 * @param mouse  Current mouse cursor screen position.
 * @param dt     Frame delta time in seconds.
 * @return       true if button was clicked (hovered and left mouse button released).
 */
bool UIButtonDraw(UIButton *b, Vector2 mouse, float dt)
{
    if (b == NULL) {
        return false;
    }

    /* 1. Collision test: mouse cursor vs button bounding rectangle */
    bool isOver = CheckCollisionPointRec(mouse, b->bounds);

    /* 2. Exponential smoothing towards target hover state */
    float targetHover = (isOver && b->enabled) ? 1.0f : 0.0f;
    b->hover += (targetHover - b->hover) * fminf(1.0f, dt * 12.0f);
    b->hover = ClampF(b->hover, 0.0f, 1.0f);

    /* 3. Center-anchored bounds expansion */
    float scale = 1.0f + b->hover * 0.025f;
    float expandedW = b->bounds.width * scale;
    float expandedH = b->bounds.height * scale;
    Rectangle drawRec = {
        b->bounds.x - (expandedW - b->bounds.width) * 0.5f,
        b->bounds.y - (expandedH - b->bounds.height) * 0.5f,
        expandedW,
        expandedH
    };

    /* 4. Dynamic color interpolation based on hover progress */
    unsigned char bgR = (unsigned char)ClampF(12.0f + b->hover * 12.0f, 0.0f, 255.0f);
    unsigned char bgG = (unsigned char)ClampF(30.0f + b->hover * 30.0f, 0.0f, 255.0f);
    unsigned char bgB = (unsigned char)ClampF(55.0f + b->hover * 55.0f, 0.0f, 255.0f);
    Color fillColor = (Color){ bgR, bgG, bgB, 245 };

    unsigned char borderR = (unsigned char)ClampF(60.0f + b->hover * 80.0f, 0.0f, 255.0f);
    Color borderColor = (Color){ borderR, 235, 255, 255 };

    /* 5. Render rounded body and glow border */
    DrawRectangleRounded(drawRec, 0.18f, 12, fillColor);
    DrawRectangleRoundedLinesEx(drawRec, 0.18f, 12, 2.0f, borderColor);

    /* 6. Render centered text label with slight font scale on hover */
    int fontSize = (int)(22.0f + b->hover * 1.5f);
    int textWidth = MeasureText(b->label, fontSize);
    int textX = (int)(drawRec.x + (drawRec.width - (float)textWidth) * 0.5f);
    int textY = (int)(drawRec.y + (drawRec.height - (float)fontSize) * 0.5f);

    DrawText(b->label, textX, textY, fontSize, RAYWHITE);

    /* 7. Return true if clicked on this frame */
    return isOver && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && b->enabled;
}

/* ---------------------------------------------------------------------------
 * Title Banner Rendering
 * --------------------------------------------------------------------------- */

/*
 * UIDrawTitle
 * -----------
 * Renders the primary neon title with a floating sinusoidal height oscillation.
 *
 * @param title     Main title text string.
 * @param subtitle  Subtitle description string.
 * @param time      Continuous game clock time in seconds.
 */
void UIDrawTitle(const char *title, const char *subtitle, float time)
{
    int screenWidth = GetScreenWidth();

    /* Sinusoidal vertical bobbing: y = 90 + sin(time * 1.2) * 4 */
    float floatY = 90.0f + sinf(time * 1.2f) * 4.0f;

    /* Primary glowing cyan headline (font size 50) */
    int titleWidth = MeasureText(title, 50);
    int titleX = screenWidth / 2 - titleWidth / 2;
    Color titleColor = (Color){ 70, 235, 255, 255 };
    DrawText(title, titleX, (int)floatY, 50, titleColor);

    /* Subtitle tagline (font size 18) positioned 60 pixels below title */
    if (subtitle != NULL) {
        int subWidth = MeasureText(subtitle, 18);
        int subX = screenWidth / 2 - subWidth / 2;
        Color subColor = (Color){ 170, 200, 230, 255 };
        DrawText(subtitle, subX, (int)(floatY + 60.0f), 18, subColor);
    }
}
