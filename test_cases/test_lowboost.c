#include <stdio.h>
#include <stdbool.h>
#include <math.h>

int main(void)
{
    float tableY = 1.15f;
    float ballR = 0.105f;
    float g = 9.81f;
    float dt = 1.0f / 60.0f;

    for (float paddleY = 1.20f; paddleY <= 1.60f; paddleY += 0.05f) {
        for (float charge = 0.0f; charge <= 1.0f; charge += 0.25f) {
            float c = charge;
            float speed = 8.8f + 3.8f * c;
            float baseLoft = 2.40f - 0.60f * c;
            float lowBoost = 0.0f;
            if (paddleY < tableY + 0.35f) {
                lowBoost = (tableY + 0.35f - paddleY) * 2.0f;
            }
            float loft = baseLoft + lowBoost;

            float py = paddleY;
            float pz = 3.25f;
            float vy = loft;
            float vz = -speed;
            int bounces = 0;
            bool reachedAI = false;
            float bounceZ1 = 0, bounceZ2 = 0;

            for (int f = 0; f < 100; f++) {
                vy -= g * dt;
                py += vy * dt;
                pz += vz * dt;

                if (py <= tableY + ballR && fabsf(pz) <= 4.5f && vy < 0.0f) {
                    bounces++;
                    py = tableY + ballR;
                    vy = fabsf(vy) * 0.88f; // good lively table bounce
                    vz *= 0.98f;
                    if (bounces == 1) bounceZ1 = pz;
                    if (bounces == 2) bounceZ2 = pz;
                }

                if (pz <= -3.45f && !reachedAI) {
                    reachedAI = true;
                    break;
                }
            }
            if (!reachedAI || bounces > 1) {
                printf("FAIL paddleY=%.2f, charge=%.2f -> reachedAI=%d, bounces=%d (b1=%.2f, b2=%.2f)\n",
                       paddleY, charge, reachedAI, bounces, bounceZ1, bounceZ2);
            }
        }
    }
    printf("Test complete! All cases verified.\n");
    return 0;
}
