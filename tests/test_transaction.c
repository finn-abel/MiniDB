#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "transaction/transaction.h"

typedef struct {
    bool was_called;
    DBStatus status;
} CallbackContext;

static DBStatus test_callback(void *context) {
    CallbackContext *callback_context = context;

    if (callback_context == NULL) {
        return DB_ERROR;
    }

    callback_context->was_called = true;

    return callback_context->status;
}

static void test_transaction_init(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction.id == 0);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
    assert(transaction.autocommit == true);
}

static void test_transaction_init_rejects_null(void) {
    assert(transaction_init(NULL) == DB_ERROR);
}

static void test_transaction_begin_and_commit(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_begin(&transaction) == DB_OK);
    assert(transaction.id != 0);
    assert(transaction.state == TRANSACTION_STATE_ACTIVE);
    assert(transaction_commit(&transaction) == DB_OK);
    assert(transaction.id == 0);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
}

static void test_transaction_begin_rejects_active_transaction(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_begin(&transaction) == DB_OK);
    assert(transaction_begin(&transaction) == DB_ERROR);
    assert(transaction_rollback(&transaction) == DB_OK);
}

static void test_transaction_commit_rejects_inactive_transaction(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_commit(&transaction) == DB_ERROR);
}

static void test_transaction_rollback(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_begin(&transaction) == DB_OK);
    assert(transaction_rollback(&transaction) == DB_OK);
    assert(transaction.id == 0);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
}

static void test_transaction_rollback_rejects_inactive_transaction(void) {
    Transaction transaction;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_rollback(&transaction) == DB_ERROR);
}

static void test_transaction_execute_autocommit_commits_success(void) {
    Transaction transaction;
    CallbackContext context;

    context.was_called = false;
    context.status = DB_OK;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_execute_autocommit(&transaction, test_callback, &context) == DB_OK);

    assert(context.was_called == true);
    assert(transaction.id == 0);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
}

static void test_transaction_execute_autocommit_rolls_back_failure(void) {
    Transaction transaction;
    CallbackContext context;

    context.was_called = false;
    context.status = DB_NOT_FOUND;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_execute_autocommit(&transaction, test_callback, &context) == DB_NOT_FOUND);

    assert(context.was_called == true);
    assert(transaction.id == 0);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
}

static void test_transaction_execute_autocommit_rejects_null_inputs(void) {
    Transaction transaction;
    CallbackContext context;

    context.was_called = false;
    context.status = DB_OK;

    assert(transaction_init(&transaction) == DB_OK);
    assert(transaction_execute_autocommit(NULL, test_callback, &context) == DB_ERROR);
    assert(transaction_execute_autocommit(&transaction, NULL, &context) == DB_ERROR);
}

static void test_transaction_execute_autocommit_rejects_manual_mode(void) {
    Transaction transaction;
    CallbackContext context;

    context.was_called = false;
    context.status = DB_OK;

    assert(transaction_init(&transaction) == DB_OK);
    transaction.autocommit = false;

    assert(transaction_execute_autocommit(&transaction, test_callback, &context) == DB_ERROR);
    assert(context.was_called == false);
    assert(transaction.state == TRANSACTION_STATE_IDLE);
}

int main(void) {
    test_transaction_init();
    test_transaction_init_rejects_null();
    test_transaction_begin_and_commit();
    test_transaction_begin_rejects_active_transaction();
    test_transaction_commit_rejects_inactive_transaction();
    test_transaction_rollback();
    test_transaction_rollback_rejects_inactive_transaction();
    test_transaction_execute_autocommit_commits_success();
    test_transaction_execute_autocommit_rolls_back_failure();
    test_transaction_execute_autocommit_rejects_null_inputs();
    test_transaction_execute_autocommit_rejects_manual_mode();

    printf("All transaction tests passed.\n");

    return 0;
}
