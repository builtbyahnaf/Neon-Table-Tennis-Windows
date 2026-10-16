#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    PhysicsWorld w;
    PhysicsInit(&w);

    // Let's test a smash hit by player from a realistic height
    // Paddle at y = 1.35 (just above tableY 1.15)
    w.ball.position = (Vector3){ 0.0f, 1.35f, 3.25f };
    w.ball.velocity = (Vector3){ 0.0f, 0.5f, 6.0f }; // incoming ball
    w.lastHitter = 2; // AI hit it previously
    w.ball.inPlay = true;

    w.player.position = (Vector3){ 0.0f, 1.35f, 3.45f };

    PhysicsInput in = {
        .target = { 0.0f, 1.35f, 3.45f },
        .hit = true,
        .smash = true,
        .charge = 1.0f
    };

    int ps = 0, as = 0, rally = 0;
    float dt = 1.0f / 60.0f;

    for (int f = 0; f < 60; f++) {
        int oldH = w.lastHitter, oldR = rally, oldP = ps, oldA = as;
        PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);

        if (w.lastHitter != oldH) {
            printf("[F%2d] SMASH HIT! pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f) spin=(%.2f, %.2f, %.2f)\n",
                   f, w.ball.position.x, w.ball.position.y, w.ball.position.z,
                   w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z,
                   w.ball.spin.x, w.ball.spin.y, w.ball.spin.z);
        }
        if (rally != oldR) {
            printf("[F%2d] BOUNCE rally=%d pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
                   f, rally, w.ball.position.x, w.ball.position.y, w.ball.position.z,
                   w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z);
        }
        if (ps != oldP || as != oldA) {
            printf("[F%2d] POINT SCORED! Player=%d, AI=%d (inPlay=%d) pos=(%.2f, %.2f, %.2f)\n",
                   f, ps, as, w.ball.inPlay, w.ball.position.x, w.ball.position.y, w.ball.position.z);
            break;
        }
    }
    return 0;
}
