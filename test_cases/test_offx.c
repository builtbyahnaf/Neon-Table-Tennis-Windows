#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    float dt = 1.0f / 60.0f;
    for (float offX = -0.40f; offX <= 0.40f; offX += 0.10f) {
        PhysicsWorld w;
        PhysicsInit(&w);
        float paddleX = 0.0f;
        float ballX = paddleX + offX;
        w.ball.position = (Vector3){ ballX, 1.45f, 3.25f };
        w.ball.velocity = (Vector3){ 0.0f, 0.0f, 6.0f };
        w.lastHitter = 2;
        w.ball.inPlay = true;
        w.player.position = (Vector3){ paddleX, 1.45f, 3.45f };

        PhysicsInput in = {
            .target = { paddleX, 1.45f, 3.45f },
            .hit = true,
            .smash = false,
            .charge = 0.0f
        };

        int ps = 0, as = 0, rally = 0;
        bool reachedAI = false;
        int endFrame = -1;
        char reason[64] = "timed out";

        for (int f = 0; f < 100; f++) {
            PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);
            if (w.lastHitter == 2) {
                reachedAI = true;
                endFrame = f;
                snprintf(reason, sizeof(reason), "AI returned");
                break;
            }
            if (!w.ball.inPlay) {
                endFrame = f;
                snprintf(reason, sizeof(reason), "Died: %s (pos=%.2f,%.2f,%.2f)",
                         (as > 0) ? "AI scored" : "Player scored",
                         w.ball.position.x, w.ball.position.y, w.ball.position.z);
                break;
            }
        }
        printf("offX=%.2f -> %s at F%d (reachedAI=%d)\n", offX, reason, endFrame, reachedAI);
    }
    return 0;
}
