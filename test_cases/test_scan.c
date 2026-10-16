#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    float dt = 1.0f / 60.0f;
    for (float paddleY = 1.20f; paddleY <= 1.60f; paddleY += 0.05f) {
        for (float charge = 0.0f; charge <= 1.0f; charge += 0.25f) {
            PhysicsWorld w;
            PhysicsInit(&w);
            w.ball.position = (Vector3){ 0.0f, paddleY, 3.25f };
            w.ball.velocity = (Vector3){ 0.0f, 0.0f, 6.0f };
            w.lastHitter = 2;
            w.ball.inPlay = true;
            w.player.position = (Vector3){ 0.0f, paddleY, 3.45f };

            PhysicsInput in = {
                .target = { 0.0f, paddleY, 3.45f },
                .hit = true,
                .smash = (charge > 0.5f),
                .charge = charge
            };

            int ps = 0, as = 0, rally = 0;
            bool reachedOpponent = false;
            int endFrame = -1;
            char reason[64] = "timed out";

            for (int f = 0; f < 100; f++) {
                PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);
                if (w.lastHitter == 2) {
                    reachedOpponent = true;
                    endFrame = f;
                    snprintf(reason, sizeof(reason), "AI returned");
                    break;
                }
                if (!w.ball.inPlay) {
                    endFrame = f;
                    snprintf(reason, sizeof(reason), "Died: %s (pos=%.2f,%.2f,%.2f, netHit=%d)",
                             (as > 0) ? "AI scored" : "Player scored",
                             w.ball.position.x, w.ball.position.y, w.ball.position.z, w.netHit);
                    break;
                }
            }
            if (!reachedOpponent) {
                printf("FAIL at paddleY=%.2f, charge=%.2f -> %s at F%d\n", paddleY, charge, reason, endFrame);
            }
        }
    }
    printf("Scan complete.\n");
    return 0;
}
