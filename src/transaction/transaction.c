#include "buffer/buffer_pool.h"
#include "transaction/transaction.h"

/*
 * Transaction IDs are process-local for now. They give each active transaction
 * a stable identity that future WAL or lock-manager code can attach records to.
 */
static uint64_t next_transaction_id = 1;

static uint64_t transaction_next_id(void) {
    uint64_t id = next_transaction_id;

    next_transaction_id++;

    /*
     * Keep zero reserved for "no active transaction" even if the counter wraps.
     */
    if (next_transaction_id == 0) {
        next_transaction_id = 1;
    }

    return id;
}

DBStatus transaction_init(Transaction *transaction) {
    if (transaction == NULL) {
        return DB_ERROR;
    }

    transaction->id = 0;
    transaction->state = TRANSACTION_STATE_IDLE;
    /*
     * SQL engines commonly default to autocommit: each statement is wrapped in
     * its own transaction unless the user explicitly starts one.
     */
    transaction->autocommit = true;

    return DB_OK;
}

DBStatus transaction_begin(Transaction *transaction) {
    if (transaction == NULL) {
        return DB_ERROR;
    }

    if (transaction->state == TRANSACTION_STATE_ACTIVE) {
        /*
         * Nested transactions/savepoints are future work. For now, one
         * Transaction object can represent only one active transaction.
         */
        return DB_ERROR;
    }

    transaction->id = transaction_next_id();
    transaction->state = TRANSACTION_STATE_ACTIVE;

    return DB_OK;
}

DBStatus transaction_commit(Transaction *transaction) {
    if (transaction == NULL) {
        return DB_ERROR;
    }

    if (transaction->state != TRANSACTION_STATE_ACTIVE) {
        return DB_ERROR;
    }

    /*
     * Autocommit needs a real persistence point even before WAL exists.
     * Flushing all dirty pages is conservative but correct for this stage.
     */
    DBStatus status = buffer_pool_flush_all();

    if (status != DB_OK) {
        /*
         * If dirty pages cannot be flushed, keep the transaction active so the
         * caller can decide whether to retry, rollback, or report the failure.
         */
        return status;
    }

    transaction->state = TRANSACTION_STATE_IDLE;
    transaction->id = 0;

    return DB_OK;
}

DBStatus transaction_rollback(Transaction *transaction) {
    if (transaction == NULL) {
        return DB_ERROR;
    }

    if (transaction->state != TRANSACTION_STATE_ACTIVE) {
        return DB_ERROR;
    }

    /*
     * There is no undo log yet, so rollback only ends the transaction state.
     * Once logging exists, this function will undo the writes made by id.
     */
    transaction->state = TRANSACTION_STATE_IDLE;
    transaction->id = 0;

    return DB_OK;
}

DBStatus transaction_execute_autocommit(
    Transaction *transaction,
    TransactionStatementCallback callback,
    void *context
) {
    if (transaction == NULL || callback == NULL) {
        return DB_ERROR;
    }

    if (!transaction->autocommit) {
        /*
         * Manual transaction mode will later be driven by BEGIN/COMMIT/ROLLBACK
         * statements. In that mode, callers should not use this wrapper.
         */
        return DB_ERROR;
    }

    DBStatus status = transaction_begin(transaction);

    if (status != DB_OK) {
        return status;
    }

    status = callback(context);

    if (status != DB_OK) {
        /*
         * Without undo logging, rollback cannot reverse page changes yet, but
         * it still restores transaction state so the next statement can run.
         */
        DBStatus rollback_status = transaction_rollback(transaction);

        if (rollback_status != DB_OK) {
            return rollback_status;
        }

        return status;
    }

    return transaction_commit(transaction);
}
