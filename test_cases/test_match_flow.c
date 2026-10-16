#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define BALL_R        0.105f
#define TABLE_W       5.0f
#define TABLE_L       9.0f
#define HALF_W        (TABLE_W * 0.5f)
#define HALF_L        (TABLE_L * 0.5f)
#define MAX_AI_SPEED  5.5f

typedef struct { float x, y, z; } V3;

typedef struct {
    V3 position;
    V3 velocity;
    V3 spin;
    float radius;
    bool inPlay;
} Ball;

typedef struct {
    V3 position;
    V3 velocity;
    float halfWidth;
    float halfHeight;
} Paddle;

typedef struct {
    Ball ball;
    Paddle player;
    Paddle ai;
    float tableY;
    float gravity;
    float drag;
    float serveTimer;
    int lastHitter;
    int bouncesOnReceiverSide;
} World;

static float ClampF(float v, float mn, float mx) {
    return v < mn ? mn : (v > mx ? mx : v);
}

void Serve(World *w, int towardPlayer) {
    float dir = (towardPlayer > 0) ? 1.0f : -1.0f;
    float serveZ = -dir * 2.8f;
    float serveX = 0.0f;

    w->ball.position = (V3){ serveX, w->tableY + 0.50f, serveZ };
    w->ball.velocity = (V3){ 0.0f, 2.5f, dir * 6.5f };
    w->ball.inPlay = true;
    w->lastHitter = (towardPlayer > 0) ? 2 : 1;
    w->bouncesOnReceiverSide = 0;
}

void Hit(World *w, Paddle *p, bool isPlayer, float charge) {
    float dir = isPlayer ? -1.0f : 1.0f;
    float c = ClampF(charge, 0.0f, 1.0f);

    float speed = 8.8f + 3.8f * c;
    float loft = 2.30f - 0.65f * c;

    float offX = ClampF((w->ball.position.x - p->position.x) / p->halfWidth, -1.0f, 1.0f);

    w->ball.velocity.x = offX * 1.5f;
    w->ball.velocity.y = loft;
    w->ball.velocity.z = dir * speed;

    w->ball.position.z = isPlayer ? (p->position.z - w->ball.radius - 0.10f)
                                  : (p->position.z + w->ball.radius + 0.10f);

    w->lastHitter = isPlayer ? 1 : 2;
    w->bouncesOnReceiverSide = 0;
}

int main(void) {
    printf("Simulating full rally flow with realistic inputs...\n");
    World w;
    memset(&w, 0, sizeof(w));
    w.tableY = 1.15f;
    w.gravity = 9.81f;
    w.drag = 0.012f;
    w.ball.radius = BALL_R;
    w.player = (Paddle){ {0, 1.50f, 3.45f}, {0,0,0}, 0.52f, 0.38f };
    w.ai     = (Paddle){ {0, 1.50f, -3.45f}, {0,0,0}, 0.52f, 0.38f };

    Serve(&w, 1); // AI serves to player

    float dt = 1.0f / 60.0f;
    int rally = 0;
    int ps = 0, as = 0;

    for (int frame = 0; frame < 900; frame++) {
        if (!w.ball.inPlay) break;

        // Physics integrate
        w.ball.velocity.y -= w.gravity * dt;
        w.ball.position.x += w.ball.velocity.x * dt;
        w.ball.position.y += w.ball.velocity.y * dt;
        w.ball.position.z += w.ball.velocity.z * dt;

        // Player paddle follows ball
        w.player.position.x += (w.ball.position.x - w.player.position.x) * 0.5f;
        w.player.position.y += (w.ball.position.y - w.player.position.y) * 0.5f;

        // AI paddle follows ball
        w.ai.position.x += (w.ball.position.x - w.ai.position.x) * 0.3f;
        w.ai.position.y += (w.ball.position.y - w.ai.position.y) * 0.3f;

        // Table bounce
        if (w.ball.position.y <= w.tableY + w.ball.radius &&
            fabsf(w.ball.position.z) <= HALF_L &&
            w.ball.velocity.y <= 0.0f)
        {
            w.ball.position.y = w.tableY + w.ball.radius;
            w.ball.velocity.y = fabsf(w.ball.velocity.y) * 0.82f;
            w.ball.velocity.z *= 0.98f;

            bool isReceiverSide = (w.lastHitter == 1 && w.ball.position.z < 0.0f) ||
                                  (w.lastHitter == 2 && w.ball.position.z > 0.0f);
            if (isReceiverSide) {
                w.bouncesOnReceiverSide++;
                if (w.bouncesOnReceiverSide == 1) {
                    rally++;
                    printf("[F%3d] Table Bounce! Rally=%d, z=%.2fm\n", frame, rally, w.ball.position.z);
                } else {
                    printf("[F%3d] Double Bounce! Rally=%d. Hitter %d scores!\n", frame, rally, w.lastHitter);
                    w.ball.inPlay = false;
                }
            }
        }

        // Player contact
        if (w.ball.inPlay && w.ball.velocity.z > 0.0f && w.lastHitter != 1) {
            float dz = fabsf(w.ball.position.z - w.player.position.z);
            float dx = fabsf(w.ball.position.x - w.player.position.x);
            float dy = fabsf(w.ball.position.y - w.player.position.y);
            if (dz <= 0.30f && dx <= w.player.halfWidth + w.ball.radius && dy <= w.player.halfHeight + w.ball.radius) {
                // Every 3rd hit, player uses a charged smash!
                float charge = (rally % 3 == 0) ? 1.0f : 0.0f;
                Hit(&w, &w.player, true, charge);
                printf("[F%3d] PLAYER HIT (charge=%.1f)! Ball vz=%.2f, vy=%.2f\n",
                       frame, charge, w.ball.velocity.z, w.ball.velocity.y);
            }
        }

        // AI contact
        if (w.ball.inPlay && w.ball.velocity.z < 0.0f && w.lastHitter != 2) {
            float dz = fabsf(w.ball.position.z - w.ai.position.z);
            float dx = fabsf(w.ball.position.x - w.ai.position.x);
            float dy = fabsf(w.ball.position.y - w.ai.position.y);
            if (dz <= 0.30f && dx <= w.ai.halfWidth + w.ball.radius && dy <= w.ai.halfHeight + w.ball.radius) {
                Hit(&w, &w.ai, false, 0.2f);
                printf("[F%3d] AI HIT! Ball vz=%.2f, vy=%.2f\n",
                       frame, w.ball.velocity.z, w.ball.velocity.y);
            }
        }
    }
    printf("Simulation finished with %d continuous rally hits!\n", rally);
    return 0;
}
