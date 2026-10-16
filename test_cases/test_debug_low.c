#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    float dt = 1.0f / 60.0f;
    PhysicsWorld w;
    PhysicsInit(&w);
    float paddleY = 1.20f;
    w.ball.position = (Vector3){ 0.0f, paddleY, 3.25f };
    w.ball.velocity = (Vector3){ 0.0f, 0.0f, 6.0f };
    w.lastHitter = 2;
    w.ball.inPlay = true;
    w.player.position = (Vector3){ 0.0f, paddleY, 3.45f };

    PhysicsInput in = {
        .target = { 0.0f, paddleY, 3.45f },
        .hit = true,
        .smash = false,
        .charge = 0.0f
    };

    int ps = 0, as = 0, rally = 0;
    for (int f = 0; f < 50; f++) {
        int oldR = rally, oldP = ps, oldA = as;
        PhysicsUpdate(&w, dt, in, 0.65f, &ps, &as, &rally);
        printf("[F%2d] Ball pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f) AI=(%.2f, %.2f, %.2f) bRecv=%d\n",
               f, w.ball.position.x, w.ball.position.y, w.ball.position.z,
               w.ball.velocity.x, w.ball.velocity.y, w.ball.velocity.z,
               w.ai.position.x, w.ai.position.y, w.ai.position.z,
               w.bouncesOnReceiverSide);
        if (ps != oldP || as != oldA) {
            printf(">>> POINT at F%d! Player=%d, AI=%d (inPlay=%d)\n", f, ps, as, w.ball.inPlay);
            break;
        }
    }
    return 0;
}
