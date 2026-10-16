#include <stdio.h>
#include <stdbool.h>
#include <math.h>

typedef struct {
    float x, y, z;
} V3;

int main(void)
{
    float tableY = 1.15f;
    float ballR = 0.105f;
    float g = 9.81f;
    float dt = 1.0f / 60.0f;

    float charges[4] = { 0.0f, 0.4f, 0.8f, 1.0f };

    for (int cIdx = 0; cIdx < 4; cIdx++) {
        float charge = charges[cIdx];
        float speed = 8.8f + 3.8f * charge; // 8.8 to 12.6 m/s
        float loft = 2.3f - 0.65f * charge; // 2.30 down to 1.65 m/s

        printf("\n--- Testing Charge = %.2f (Speed=%.1f m/s, Loft=%.2f m/s) ---\n", charge, speed, loft);

        V3 pos = { 0.0f, 1.45f, 3.25f };
        V3 vel = { 0.0f, loft, -speed };

        int bounces = 0;
        float netY = -1.0f;
        float bounceZ = 999.0f;
        float aiZ = -3.45f;
        float aiArrivalY = -1.0f;
        bool reachedAI = false;

        for (int frame = 0; frame < 120; frame++) {
            vel.y -= g * dt;
            pos.x += vel.x * dt;
            pos.y += vel.y * dt;
            pos.z += vel.z * dt;

            if (pos.z <= 0.0f && netY < 0.0f) netY = pos.y;

            if (pos.y <= tableY + ballR && fabsf(pos.z) <= 4.5f && vel.y < 0.0f) {
                bounces++;
                pos.y = tableY + ballR;
                vel.y = fabsf(vel.y) * 0.82f;
                vel.z *= 0.98f;
                if (bounces == 1) bounceZ = pos.z;
            }

            if (pos.z <= aiZ && !reachedAI) {
                reachedAI = true;
                aiArrivalY = pos.y;
                printf("Hit opponent at frame %d! Net Y=%.2fm (clearance=%.2fm), Bounce at z=%.2fm, Height at AI=%.2fm (bounces=%d)\n",
                       frame, netY, netY - (tableY + 0.1525f), bounceZ, aiArrivalY, bounces);
                break;
            }
        }
        if (!reachedAI) {
            printf("FAILED! netY=%.2f, bounces=%d\n", netY, bounces);
        }
    }
    return 0;
}
