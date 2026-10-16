#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    printf("--- TEST 1: SMASH by player ---\n");
    {
        PhysicsWorld w;
        PhysicsInit(&w);
        int ps = 0, as = 0, rally = 0;
        float dt = 1.0f / 60.0f;
        PhysicsInput in = { .target = {0, 1.55f, 3.45f}, .hit = true, .smash = true, .charge = 1.0f };
        for (int frame = 0; frame < 200; frame++) {
            in.target.x = w.ball.position.x;
            in.target.y = w.ball.position.y;
            int oldH = w.lastHitter, oldR = rally, oldP = ps, oldA = as;
            PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);
            if (w.lastHitter != oldH)
                printf("[F%3d] HIT %s pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
                       frame, w.lastHitter == 1 ? "PLAYER" : "AI",
                       w.ball.position.x, w.ball.position.y, w.ball.position.z,
                       w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z);
            if (rally != oldR)
                printf("[F%3d] TABLE BOUNCE (Rally %d) pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
                       frame, rally,
                       w.ball.position.x, w.ball.position.y, w.ball.position.z,
                       w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z);
            if (ps != oldP || as != oldA)
                printf("[F%3d] SCORE! Player=%d, AI=%d (inPlay=%d)\n", frame, ps, as, w.ball.inPlay);
        }
    }

    printf("\n--- TEST 2: PLAYER SERVING (towardPlayer <= 0) ---\n");
    {
        PhysicsWorld w;
        PhysicsInit(&w);
        PhysicsServe(&w, -1); // Player serves!
        int ps = 0, as = 0, rally = 0;
        float dt = 1.0f / 60.0f;
        PhysicsInput in = { .target = {0, 1.55f, 3.45f}, .hit = true, .smash = false, .charge = 0.0f };
        for (int frame = 0; frame < 150; frame++) {
            int oldH = w.lastHitter, oldR = rally, oldP = ps, oldA = as;
            PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);
            if (w.lastHitter != oldH)
                printf("[F%3d] HIT %s pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
                       frame, w.lastHitter == 1 ? "PLAYER" : "AI",
                       w.ball.position.x, w.ball.position.y, w.ball.position.z,
                       w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z);
            if (rally != oldR)
                printf("[F%3d] TABLE BOUNCE (Rally %d) pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
                       frame, rally,
                       w.ball.position.x, w.ball.position.y, w.ball.position.z,
                       w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z);
            if (ps != oldP || as != oldA)
                printf("[F%3d] SCORE! Player=%d, AI=%d (inPlay=%d)\n", frame, ps, as, w.ball.inPlay);
        }
    }

    return 0;
}
