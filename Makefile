CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g
INCLUDES = -Iinclude

TARGET = MiniDB

# Add normal project source files here.
SRC = src/main.c src/db.c src/value.c src/row.c src/rid.c src/page.c src/pager.c \
      src/record.c src/schema.c src/table.c src/catalog.c src/sql/lexer.c src/sql/ast.c \
      src/sql/parser.c src/sql/binder.c src/execution/plan.c src/execution/planner.c src/execution/executor.c \
      src/util/error.c src/buffer/buffer_pool.c src/buffer/replacer.c src/storage/free_space.c \
      src/index/index.c src/index/btree.c src/transaction/transaction.c

# Add test source files here.
TEST_SRC = tests/test_value.c tests/test_row.c tests/test_row_serialization.c tests/test_rid.c tests/test_page.c tests/test_pager.c \
           tests/test_record.c tests/test_schema.c tests/test_db.c tests/test_catalog.c tests/test_table.c tests/test_buffer_pool.c tests/test_free_space.c tests/test_lexer.c \
           tests/test_ast.c tests/test_parser.c tests/test_binder.c tests/test_planner.c tests/test_executor.c tests/test_error.c \
           tests/test_index.c tests/test_btree.c tests/test_transaction.c

OBJ = $(SRC:.c=.o)

# Source files needed for tests should not include src/main.c,
# because each test file has its own main function.
TEST_SUPPORT_SRC = $(filter-out src/main.c, $(SRC))
TEST_SUPPORT_OBJ = $(TEST_SUPPORT_SRC:.c=.o)

TEST_TARGETS = $(TEST_SRC:tests/%.c=%)
TEST_OBJ = $(TEST_SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%: tests/%.o $(TEST_SUPPORT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TEST_TARGETS)
	@set -e; \
	for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		./$$test; \
		echo ""; \
	done; \
	$(MAKE) clean

clean:
	rm -f $(OBJ) $(TEST_SUPPORT_OBJ) $(TEST_OBJ) $(TARGET) $(TEST_TARGETS)
	rm -rf *.dSYM tests/*.dSYM

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run test
