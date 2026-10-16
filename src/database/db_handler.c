/*
 * =============================================================================
 * db_handler.c  —  Neon Table Tennis  |  Persistent SQLite Database Handler
 * =============================================================================
 *
 * IMPLEMENTATION DETAILS
 * ----------------------
 * Implements local statistics persistence using SQLite 3.
 *
 * FEATURES & RESILIENCE
 * ---------------------
 * 1. WAL (Write-Ahead Logging):
 *    Enables concurrent readers and atomic commits without lock contention.
 *
 * 2. Prepared Statements & Parameter Binding:
 *    All inserts, updates, and queries use `sqlite3_prepare_v2` and `sqlite3_bind_*`
 *    to avoid SQL string concatenation and ensure proper type coercion.
 *
 * 3. Graceful WAL Checkpointing:
 *    On DBClose(), runs a passive WAL checkpoint (`sqlite3_wal_checkpoint_v2`)
 *    to merge write buffers back into the main database file.
 *
 * 4. Error Isolation:
 *    If the database fails to open or execute a query, it reports via `d->error`
 *    and sets `d->available = false`. The rest of the game continues without interruption.
 *
 * =============================================================================
 */

#include "db_handler.h"
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Database Lifecycle
 * --------------------------------------------------------------------------- */

/*
 * DBOpen
 * ------
 * Connects to the SQLite database file and initializes the schemas.
 *
 * @param d     Pointer to DbHandler struct.
 * @param path  Filesystem path to the database file.
 * @return      true on success, false on error.
 */
bool DBOpen(DbHandler *d, const char *path)
{
    if (d == NULL || path == NULL) {
        return false;
    }

    memset(d, 0, sizeof(*d));

    /* 1. Open SQLite database connection */
    int rc = sqlite3_open(path, &d->db);
    if (rc != SQLITE_OK) {
        snprintf(d->error, sizeof(d->error), "SQLite open failed: %s",
                 d->db ? sqlite3_errmsg(d->db) : "out of memory");
        if (d->db != NULL) {
            sqlite3_close(d->db);
            d->db = NULL;
        }
        return false;
    }

    /* 2. Configure Write-Ahead Logging (WAL) mode for performance and durability */
    char *err = NULL;
    rc = sqlite3_exec(d->db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        snprintf(d->error, sizeof(d->error), "PRAGMA WAL failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        /* Non-fatal: continue even if WAL is unsupported on the filesystem */
    }

    /* 3. Schema initialization: matches table */
    const char *sqlMatches =
        "CREATE TABLE IF NOT EXISTS matches ("
        "    id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    score      INTEGER NOT NULL,"
        "    rallies    INTEGER NOT NULL,"
        "    won        INTEGER NOT NULL,"
        "    created_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ");";

    rc = sqlite3_exec(d->db, sqlMatches, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        snprintf(d->error, sizeof(d->error), "Schema matches failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(d->db);
        d->db = NULL;
        return false;
    }

    /* 4. Schema initialization: stats table */
    const char *sqlStats =
        "CREATE TABLE IF NOT EXISTS stats ("
        "    key   TEXT PRIMARY KEY,"
        "    value INTEGER NOT NULL"
        ");";

    rc = sqlite3_exec(d->db, sqlStats, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        snprintf(d->error, sizeof(d->error), "Schema stats failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(d->db);
        d->db = NULL;
        return false;
    }

    /* 5. Initialize baseline statistic rows */
    const char *sqlInitStats =
        "INSERT OR IGNORE INTO stats (key, value) VALUES ('wins', 0);"
        "INSERT OR IGNORE INTO stats (key, value) VALUES ('losses', 0);";

    rc = sqlite3_exec(d->db, sqlInitStats, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        snprintf(d->error, sizeof(d->error), "Seed stats failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        /* Non-fatal: ignore if rows already exist */
    }

    d->available = true;
    return true;
}

/*
 * DBClose
 * -------
 * Checkpoints WAL writes and closes the SQLite connection.
 */
void DBClose(DbHandler *d)
{
    if (d == NULL || d->db == NULL) {
        return;
    }

    /* Passive WAL checkpoint to commit journal back to database file */
    sqlite3_wal_checkpoint_v2(d->db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL);

    sqlite3_close(d->db);
    memset(d, 0, sizeof(*d));
}

/* ---------------------------------------------------------------------------
 * Database Queries & Mutations
 * --------------------------------------------------------------------------- */

/*
 * DBRecordMatch
 * -------------
 * Inserts a completed match record and increments lifetime wins or losses.
 *
 * @param d        Active DbHandler instance.
 * @param score    Player points scored.
 * @param rallies  Highest rally count during match.
 * @param won      true if player won the match.
 */
void DBRecordMatch(DbHandler *d, int score, int rallies, bool won)
{
    if (d == NULL || !d->available || d->db == NULL) {
        return;
    }

    /* 1. Insert row into matches history */
    sqlite3_stmt *stmt = NULL;
    const char *insertSql = "INSERT INTO matches (score, rallies, won) VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(d->db, insertSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, score);
        sqlite3_bind_int(stmt, 2, rallies);
        sqlite3_bind_int(stmt, 3, won ? 1 : 0);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    /* 2. Increment career win or loss counter */
    const char *updateSql = "UPDATE stats SET value = value + 1 WHERE key = ?;";
    if (sqlite3_prepare_v2(d->db, updateSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, won ? "wins" : "losses", -1, SQLITE_STATIC);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

/*
 * DBGetBest
 * ---------
 * Retrieves the maximum single-match score, maximum rally, and lifetime wins/losses.
 *
 * @param d  Active DbHandler instance.
 * @return   Populated ScoreRecord struct.
 */
ScoreRecord DBGetBest(DbHandler *d)
{
    ScoreRecord record = { 0, 0, 0, 0 };

    if (d == NULL || !d->available || d->db == NULL) {
        return record;
    }

    /* 1. Query highest score and longest rally from matches */
    sqlite3_stmt *stmt = NULL;
    const char *queryMatches =
        "SELECT COALESCE(MAX(score), 0), COALESCE(MAX(rallies), 0) FROM matches;";

    if (sqlite3_prepare_v2(d->db, queryMatches, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            record.score   = sqlite3_column_int(stmt, 0);
            record.rallies = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    /* 2. Query cumulative lifetime wins and losses */
    const char *queryStats = "SELECT key, value FROM stats;";
    if (sqlite3_prepare_v2(d->db, queryStats, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *key = (const char *)sqlite3_column_text(stmt, 0);
            int value = sqlite3_column_int(stmt, 1);

            if (key != NULL) {
                if (strcmp(key, "wins") == 0) {
                    record.wins = value;
                } else if (strcmp(key, "losses") == 0) {
                    record.losses = value;
                }
            }
        }
    }
    sqlite3_finalize(stmt);

    return record;
}
