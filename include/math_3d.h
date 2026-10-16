/*
 * =============================================================================
 * math_3d.h  --  Inline 3-D Vector & Scalar Math Utilities
 * =============================================================================
 *
 * PURPOSE
 *   Provides a small, allocation-free set of Vector3 and scalar helpers that
 *   are used throughout the Neon Table Tennis physics and rendering code.
 *   Every function is declared `static inline` so the compiler can expand it
 *   at the call-site with zero function-call overhead -- critical for the
 *   fixed-timestep physics tick that runs up to several times per frame.
 *
 * DEPENDENCIES
 *   raylib.h  -- supplies the Vector3 type { float x, y, z }
 *   math.h    -- sqrtf()
 *
 * CONVENTIONS
 *   - All angles are in radians unless stated otherwise.
 *   - "normalised vector" means unit-length (magnitude == 1).
 *   - The epsilon guard in V3Norm (1e-6f) prevents divide-by-zero on
 *     near-zero vectors; callers receive a zero vector in that case.
 *
 * =============================================================================
 */

#ifndef NTT_MATH_3D_H
#define NTT_MATH_3D_H

#include "raylib.h"   /* Vector3 type */
#include <math.h>     /* sqrtf()      */


/* ---------------------------------------------------------------------------
 * V3Add -- Component-wise addition of two vectors.
 *
 *   result = { a.x + b.x,  a.y + b.y,  a.z + b.z }
 *
 * Parameters:
 *   a, b  --  Input vectors.
 *
 * Returns the vector sum a + b.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Add(Vector3 a, Vector3 b)
{
    return (Vector3){ a.x + b.x,  a.y + b.y,  a.z + b.z };
}


/* ---------------------------------------------------------------------------
 * V3Sub -- Component-wise subtraction of two vectors.
 *
 *   result = { a.x - b.x,  a.y - b.y,  a.z - b.z }
 *
 * Parameters:
 *   a  --  Minuend vector.
 *   b  --  Subtrahend vector.
 *
 * Returns the vector difference a - b.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Sub(Vector3 a, Vector3 b)
{
    return (Vector3){ a.x - b.x,  a.y - b.y,  a.z - b.z };
}


/* ---------------------------------------------------------------------------
 * V3Scale -- Multiply every component of a vector by a scalar.
 *
 *   result = { a.x * s,  a.y * s,  a.z * s }
 *
 * Parameters:
 *   a  --  Input vector.
 *   s  --  Scalar multiplier.
 *
 * Returns the scaled vector.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Scale(Vector3 a, float s)
{
    return (Vector3){ a.x * s,  a.y * s,  a.z * s };
}


/* ---------------------------------------------------------------------------
 * V3Dot -- Dot (scalar) product of two vectors.
 *
 *   result = a.x*b.x + a.y*b.y + a.z*b.z
 *
 * Geometrically, dot(a, b) == |a||b|cos(theta), where theta is the angle
 * between the two vectors. A result of 0 means the vectors are perpendicular.
 *
 * Parameters:
 *   a, b  --  Input vectors.
 *
 * Returns a signed scalar.
 * ---------------------------------------------------------------------------
 */
static inline float
V3Dot(Vector3 a, Vector3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}


/* ---------------------------------------------------------------------------
 * V3Cross -- Cross (vector) product of two vectors.
 *
 * The cross product produces a vector perpendicular to both inputs whose
 * magnitude equals |a||b|sin(theta). Component formula:
 *
 *   result.x =  a.y*b.z - a.z*b.y   (cofactor of the i-hat row)
 *   result.y =  a.z*b.x - a.x*b.z   (cofactor of the j-hat row, note sign)
 *   result.z =  a.x*b.y - a.y*b.x   (cofactor of the k-hat row)
 *
 * Derived from the 3x3 determinant:
 *   | i    j    k  |
 *   | a.x  a.y  a.z|
 *   | b.x  b.y  b.z|
 *
 * Parameters:
 *   a, b  --  Input vectors (order matters; cross product is anti-commutative:
 *             V3Cross(b,a) == -V3Cross(a,b)).
 *
 * Returns a vector perpendicular to the plane spanned by a and b.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Cross(Vector3 a, Vector3 b)
{
    return (Vector3){
        (a.y * b.z) - (a.z * b.y),   /* x component */
        (a.z * b.x) - (a.x * b.z),   /* y component */
        (a.x * b.y) - (a.y * b.x)    /* z component */
    };
}


/* ---------------------------------------------------------------------------
 * V3Len -- Euclidean (L2) length of a vector.
 *
 *   |a| = sqrt(a.x^2 + a.y^2 + a.z^2)
 *       = sqrt(dot(a, a))
 *
 * Parameters:
 *   a  --  Input vector.
 *
 * Returns the non-negative scalar length. Returns 0 for the zero vector.
 * ---------------------------------------------------------------------------
 */
static inline float
V3Len(Vector3 a)
{
    return sqrtf(V3Dot(a, a));   /* dot(a,a) == |a|^2,  sqrt gives |a| */
}


/* ---------------------------------------------------------------------------
 * V3Norm -- Return a unit-length (normalised) copy of a vector.
 *
 *   norm(a) = a / |a|    when |a| > epsilon
 *           = {0,0,0}    when |a| <= epsilon  (guard against divide-by-zero)
 *
 * The epsilon threshold (1e-6f) is chosen to be well above floating-point
 * noise but far below any physically meaningful vector length in this game.
 *
 * Parameters:
 *   a  --  Input vector (need not be unit-length).
 *
 * Returns a unit vector pointing in the same direction, or the zero vector
 * if `a` is degenerate.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Norm(Vector3 a)
{
    float len = V3Len(a);

    /* Guard: return zero vector rather than NaN/Inf on degenerate input. */
    if (len > 1e-6f)
    {
        return V3Scale(a, 1.0f / len);
    }

    return (Vector3){ 0.0f, 0.0f, 0.0f };
}


/* ---------------------------------------------------------------------------
 * V3Lerp -- Linearly interpolate between two vectors.
 *
 *   result = a + (b - a) * t
 *          = a*(1-t) + b*t
 *
 * At t=0 the result equals `a`; at t=1 the result equals `b`.
 * Values of t outside [0,1] are valid and produce extrapolation.
 *
 * Parameters:
 *   a, b  --  Start and end vectors.
 *   t     --  Interpolation parameter (typically in [0, 1]).
 *
 * Returns the interpolated vector.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
V3Lerp(Vector3 a, Vector3 b, float t)
{
    /* b - a gives the delta vector; scaling by t steps along it. */
    return V3Add(a, V3Scale(V3Sub(b, a), t));
}


/* ---------------------------------------------------------------------------
 * ClampF -- Clamp a float to the closed interval [lo, hi].
 *
 *   result = lo    if x < lo
 *          = hi    if x > hi
 *          = x     otherwise
 *
 * Parameters:
 *   x   --  Value to clamp.
 *   lo  --  Lower bound (must satisfy lo <= hi).
 *   hi  --  Upper bound.
 *
 * Returns the clamped value.
 * ---------------------------------------------------------------------------
 */
static inline float
ClampF(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}


/* ---------------------------------------------------------------------------
 * SmoothStepF -- Cubic Hermite smooth-step interpolation.
 *
 * Maps a value t in [0, 1] to a smoothly eased output in [0, 1] using the
 * cubic Hermite polynomial:
 *
 *   S(t) = t^2 * (3 - 2t)
 *        = 3t^2 - 2t^3
 *
 * Properties:
 *   - S(0) = 0,  S(1) = 1          (maps endpoints correctly)
 *   - S'(0) = 0, S'(1) = 0         (zero derivative at both ends -- smooth)
 *   - Monotonically increasing on [0, 1]
 *
 * `t` is first clamped to [0, 1] so callers need not pre-clamp.
 *
 * Use this wherever a linear lerp would look too mechanical (e.g., camera
 * transitions, UI fade-ins, ball spin-up).
 *
 * Parameters:
 *   t  --  Input parameter (clamped internally to [0, 1]).
 *
 * Returns the smoothed value in [0, 1].
 * ---------------------------------------------------------------------------
 */
static inline float
SmoothStepF(float t)
{
    t = ClampF(t, 0.0f, 1.0f);       /* guarantee t in [0, 1]      */
    return t * t * (3.0f - 2.0f * t); /* cubic Hermite: t^2*(3-2t)  */
}


/* ---------------------------------------------------------------------------
 * ReflectV3 -- Reflect an incident vector off a surface with a given normal.
 *
 * The reflection formula derived from vector decomposition:
 *
 *   r = v - 2 * dot(v, n) * n
 *
 * Derivation:
 *   Decompose v into components parallel and perpendicular to n:
 *     v_parallel = dot(v, n) * n      (projection onto n)
 *     v_perp     = v - v_parallel     (remainder, tangent to surface)
 *   Reflection negates the parallel component and keeps the perpendicular:
 *     r = v_perp - v_parallel
 *       = (v - dot(v,n)*n) - dot(v,n)*n
 *       = v - 2*dot(v,n)*n
 *
 * Parameters:
 *   v  --  Incident direction vector (need not be unit-length).
 *   n  --  Surface normal (MUST be unit-length for a correct reflection).
 *
 * Returns the reflected direction vector. Its magnitude equals that of `v`
 * when `n` is properly normalised.
 * ---------------------------------------------------------------------------
 */
static inline Vector3
ReflectV3(Vector3 v, Vector3 n)
{
    /* Scale the normal by 2*dot(v,n), then subtract from v. */
    return V3Sub(v, V3Scale(n, 2.0f * V3Dot(v, n)));
}


#endif /* NTT_MATH_3D_H */
