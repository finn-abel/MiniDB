#ifndef MINIDB_TRANSACTION_TRANSACTION_H
#define MINIDB_TRANSACTION_TRANSACTION_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

typedef enum {
    TRANSACTION_STATE_IDLE,
    TRANSACTION_STATE_ACTIVE
} TransactionState;

typedef struct {
    uint64_t id;
    TransactionState state;
    bool autocommit;
} Transaction;

typedef DBStatus (*TransactionStatementCallback)(void *context);

/*
 * Initializes transaction state.
 *
 * MiniDB starts in autocommit mode, so callers can wrap each statement with
 * transaction_execute_autocommit until explicit BEGIN/COMMIT/ROLLBACK SQL is
 * added.
 */
DBStatus transaction_init(Transaction *transaction);

/*
 * Starts a transaction.
 */
DBStatus transaction_begin(Transaction *transaction);

/*
 * Commits the active transaction.
 *
 * The first implementation makes commit durable by flushing dirty buffer-pool
 * pages. Later, this will become the place that coordinates WAL/checkpointing.
 */
DBStatus transaction_commit(Transaction *transaction);

/*
 * Rolls back the active transaction.
 *
 * This is state-only for now because MiniDB does not have undo logging yet.
 */
DBStatus transaction_rollback(Transaction *transaction);

/*
 * Runs one statement as:
 *   BEGIN
 *   statement
 *   COMMIT
 *
 * If the callback fails, the active transaction is rolled back and the
 * callback status is returned.
 */
DBStatus transaction_execute_autocommit(
    Transaction *transaction,
    TransactionStatementCallback callback,
    void *context
);

#endif
