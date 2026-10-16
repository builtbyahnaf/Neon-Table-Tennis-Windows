/*
 * =============================================================================
 * db_handler.h  —  Neon Table Tennis  |  Persistent SQLite Database Handler
 * =============================================================================
 *
 * PURPOSE
 * -------
 * Provides a lightweight, fail-safe wrapper around SQLite 3 for local career
 * statistics and high-score persistence.
 *
 * PERSISTENCE SCHEMA
 * ------------------
 *   1. matches table:
 *      CREATE TABLE IF NOT EXISTS matches (
 *          id         INTEGER PRIMARY KEY AUTOINCREMENT,
 *          score      INTEGER NOT NULL,
 *          rallies    INTEGER NOT NULL,
 *          won        INTEGER NOT NULL,
 *          created_at TEXT DEFAULT CURRENT_TIMESTAMP
 *      );
 *
 *   2. stats table:
 *      CREATE TABLE IF NOT EXISTS stats (
 *          key   TEXT PRIMARY KEY,
 *          value INTEGER NOT NULL
 *      );
 *      - Tracks aggregate keys: 'wins', 'losses'.
 *
 * RESILIENCE & ERROR HANDLING
 * ---------------------------
 * SQLite availability is completely optional at runtime:
 *   - If the database file cannot be created or opened (e.g., permissions,
 *     missing sqlite3 runtime), DBOpen() captures the error string in
 *     `d->error`, logs a warning, and sets `d->available = false`.
 *   - All DB functions check `d->available` and become safe no-ops if false.
 *   - The game never crashes or refuses to run due to a database error.
 *
 * =============================================================================
 */

#ifndef NTT_DB_HANDLER_H
#define NTT_DB_HANDLER_H

#include <stdbool.h>

/* Forward declaration of SQLite database handle to prevent header pollution */
typedef struct sqlite3 sqlite3;

/* ---------------------------------------------------------------------------
 * DbHandler
 * ---------------------------------------------------------------------------
 * Encapsulates the active SQLite database connection and error tracking.
 * --------------------------------------------------------------------------- */
typedef struct DbHandler {
    sqlite3 *db;            /* Underlying SQLite3 connection handle (or NULL) */
    bool     available;     /* True if database opened and schemas verified   */
    char     error[256];    /* Buffer storing the last error message string   */
} DbHandler;

/* ---------------------------------------------------------------------------
 * ScoreRecord
 * ---------------------------------------------------------------------------
 * Plain data transfer object holding career aggregates retrieved from the DB.
 * --------------------------------------------------------------------------- */
typedef struct ScoreRecord {
    int score;      /* Highest player score achieved in a single match        */
    int rallies;    /* Longest continuous rally recorded in any match         */
    int wins;       /* Lifetime total matches won against the AI              */
    int losses;     /* Lifetime total matches lost against the AI             */
} ScoreRecord;

/* ---------------------------------------------------------------------------
 * Database API Functions
 * --------------------------------------------------------------------------- */

/*
 * DBOpen
 * ------
 * Opens the SQLite database file at `path`, sets WAL journal mode, and creates
 * required tables and initial statistics rows if they do not yet exist.
 *
 * @param db    Pointer to the DbHandler struct to populate.
 * @param path  Filesystem path to the SQLite file (e.g. "neon_table_tennis.sqlite").
 * @return      true if successfully opened and initialized, false otherwise.
 */
bool DBOpen(DbHandler *db, const char *path);

/*
 * DBClose
 * -------
 * Flushes write-ahead logs (WAL checkpoint) and safely closes the database connection.
 * Zeros out the DbHandler structure.
 *
 * @param db  Pointer to the DbHandler to close. Safe to call on uninitialized handlers.
 */
void DBClose(DbHandler *db);

/*
 * DBRecordMatch
 * -------------
 * Records a completed match outcome in the database and updates lifetime stats.
 * Uses parameterized prepared statements to prevent injection and guarantee consistency.
 *
 * @param db       Pointer to active DbHandler.
 * @param score    Player's final match score (points scored).
 * @param rallies  Longest rally achieved during the match.
 * @param won      true if the human player won, false if AI won.
 */
void DBRecordMatch(DbHandler *db, int score, int rallies, bool won);

/*
 * DBGetBest
 * ---------
 * Queries the database for all-time record statistics (highest score, max rally,
 * cumulative wins and losses).
 *
 * @param db  Pointer to active DbHandler.
 * @return    Populated ScoreRecord struct. Returns all zeros if DB is unavailable.
 */
ScoreRecord DBGetBest(DbHandler *db);

#endif /* NTT_DB_HANDLER_H */
