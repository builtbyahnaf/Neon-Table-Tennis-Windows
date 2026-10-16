#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    printf("Testing miss scenario...\n");
    PhysicsWorld w;
    PhysicsInit(&w);

    int ps = 0, as = 0, rally = 0;
    float dt = 1.0f / 60.0f;

    // Player purposefully keeps paddle away to test AI scoring on player miss
    PhysicsInput in = {
        .target = { 2.2f, 1.50f, 3.45f }, // paddle standing off to the right
        .hit = true,
        .smash = false,
        .charge = 0.0f
    };

    for (int f = 0; f < 120; f++) {
        int oldP = ps, oldA = as;
        PhysicsUpdate(&w, dt, in, 0.70f, &ps, &as, &rally);

        if (ps != oldP || as != oldA) {
            printf("[F%2d] POINT SCORED! Player=%d, CPU=%d | bRecv=%d, inPlay=%d\n",
                   f, ps, as, w.bouncesOnReceiverSide, w.ball.inPlay);
            break;
        }
    }

    return 0;
}
