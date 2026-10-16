#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "physics.h"

int main(void)
{
    printf("=== RUNNING FULL MATCH STRESS TEST (Target Score = 11) ===\n");
    PhysicsWorld w;
    PhysicsInit(&w);

    int playerScore = 0;
    int aiScore = 0;
    int rally = 0;
    int totalRallies = 0;
    int maxRally = 0;
    int pointsPlayed = 0;

    float dt = 1.0f / 60.0f;

    for (int frame = 0; frame < 3600; frame++) { // 60 seconds of simulation
        PhysicsInput in = {
            .target = { w.ball.position.x, w.ball.position.y, 3.45f },
            .hit = true,
            .smash = (rally % 4 == 0 && rally > 0),
            .charge = (rally % 4 == 0 && rally > 0) ? 0.90f : 0.0f
        };

        int oldRally = rally;
        int oldPS = playerScore;
        int oldAS = aiScore;

        PhysicsUpdate(&w, dt, in, 0.70f, &playerScore, &aiScore, &rally);

        if (rally > oldRally) {
            totalRallies++;
            if (rally > maxRally) maxRally = rally;
        }

        if (playerScore != oldPS || aiScore != oldAS) {
            pointsPlayed++;
            printf("[Point %2d at F%4d] Winner: %s | Score: You %d - %d CPU | Last Rally Count: %d\n",
                   pointsPlayed, frame,
                   (playerScore > oldPS) ? "PLAYER" : "CPU",
                   playerScore, aiScore, oldRally);

            if ((playerScore >= 11 && playerScore - aiScore >= 2) ||
                (aiScore >= 11 && aiScore - playerScore >= 2)) {
                printf("\n>>> MATCH COMPLETE! Final Score: Player %d - %d CPU. Max rally was %d hits!\n",
                       playerScore, aiScore, maxRally);
                break;
            }
        }
    }

    printf("Total points played: %d, Max rally length: %d\n", pointsPlayed, maxRally);
    return 0;
}
