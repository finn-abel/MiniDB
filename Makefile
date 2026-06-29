CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g
INCLUDES = -Iinclude

TARGET = MiniDB
VERSION = 0.1.2
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DIST_DIR = dist
DIST_NAME = $(TARGET)-$(VERSION)
DIST_ARCHIVE = $(DIST_DIR)/$(DIST_NAME).tar.gz
DIST_FILES = Makefile README.md LICENSE VERSION .clang-format .gitignore .github DOCS include src tests
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
PACKAGE_NAME = $(TARGET)-$(VERSION)-$(UNAME_S)-$(UNAME_M)
PACKAGE_ARCHIVE = $(DIST_DIR)/$(PACKAGE_NAME).tar.gz
PACKAGE_FILES = README.md LICENSE VERSION DOCS scripts

# Add normal project source files here.
SRC = src/main.c src/db.c src/value.c src/row.c src/rid.c src/page.c src/pager.c \
      src/record.c src/schema.c src/table.c src/catalog.c src/sql/lexer.c src/sql/ast.c \
      src/sql/parser.c src/sql/binder.c src/execution/plan.c src/execution/planner.c src/execution/executor.c \
      src/util/error.c src/buffer/buffer_pool.c src/buffer/replacer.c src/storage/free_space.c \
      src/index/btree.c src/index/secondary.c src/transaction/transaction.c src/transaction/wal.c

# Add test source files here.
TEST_SRC = tests/test_value.c tests/test_row.c tests/test_row_serialization.c tests/test_rid.c tests/test_page.c tests/test_pager.c \
           tests/test_record.c tests/test_schema.c tests/test_db.c tests/test_catalog.c tests/test_table.c tests/test_buffer_pool.c tests/test_free_space.c tests/test_lexer.c \
           tests/test_ast.c tests/test_parser.c tests/test_binder.c tests/test_planner.c tests/test_executor.c tests/test_error.c \
           tests/test_btree.c tests/test_secondary.c tests/test_transaction.c tests/test_wal.c

OBJ = $(SRC:.c=.o)

# Source files needed for tests should not include src/main.c,
# because each test file has its own main function.
TEST_SUPPORT_SRC = $(filter-out src/main.c, $(SRC))
TEST_SUPPORT_OBJ = $(TEST_SUPPORT_SRC:.c=.o)

TEST_TARGETS = $(TEST_SRC:tests/%.c=%)
TEST_OBJ = $(TEST_SRC:.c=.o)

FORMAT_FILES = $(shell find src include tests -type f \( -name '*.c' -o -name '*.h' \))
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || xcrun -f clang-format 2>/dev/null)
CLANG_ANALYZE ?= $(shell command -v clang 2>/dev/null || xcrun -f clang 2>/dev/null)
CPPCHECK ?= $(shell command -v cppcheck 2>/dev/null)
SANITIZE_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -DMINIDB_VERSION=\"$(VERSION)\" -c $< -o $@

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

test-asan:
	$(MAKE) clean
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
		$(MAKE) test CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)"

fmt:
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "Error: clang-format is required for 'make fmt' but was not found."; \
		echo "Install clang-format or set CLANG_FORMAT=/path/to/clang-format."; \
		exit 1; \
	elif [ ! -f .clang-format ]; then \
		echo "Error: .clang-format is required for 'make fmt' but was not found."; \
		exit 1; \
	fi
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

fmt-check:
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "Skipping fmt-check: clang-format was not found."; \
	elif [ ! -f .clang-format ]; then \
		echo "Skipping fmt-check: .clang-format was not found."; \
	else \
		$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES); \
	fi

analyze:
	@if [ -n "$(CLANG_ANALYZE)" ]; then \
		output=$$(mktemp); \
		if ! $(CLANG_ANALYZE) --analyze -Xanalyzer -analyzer-output=text $(CFLAGS) $(INCLUDES) $(SRC) >"$$output" 2>&1; then \
			cat "$$output"; \
			rm -f "$$output"; \
			exit 1; \
		fi; \
		if [ -s "$$output" ]; then \
			cat "$$output"; \
			rm -f "$$output"; \
			exit 1; \
		fi; \
		rm -f "$$output"; \
	elif [ -n "$(CPPCHECK)" ]; then \
		$(CPPCHECK) --quiet --enable=warning,performance,portability --error-exitcode=1 $(INCLUDES) $(SRC); \
	else \
		echo "Skipping analyze: clang and cppcheck were not found."; \
	fi

check:
	$(MAKE) fmt-check
	$(MAKE) analyze
	$(MAKE) test
	$(MAKE) test-asan

dist:
	$(MAKE) clean
	rm -rf "$(DIST_DIR)/$(DIST_NAME)"
	mkdir -p "$(DIST_DIR)/$(DIST_NAME)"
	cp -R $(DIST_FILES) "$(DIST_DIR)/$(DIST_NAME)/"
	tar -czf "$(DIST_ARCHIVE)" -C "$(DIST_DIR)" "$(DIST_NAME)"
	rm -rf "$(DIST_DIR)/$(DIST_NAME)"
	@echo "Created $(DIST_ARCHIVE)"

package: $(TARGET)
	rm -rf "$(DIST_DIR)/$(PACKAGE_NAME)"
	mkdir -p "$(DIST_DIR)/$(PACKAGE_NAME)/bin"
	cp "$(TARGET)" "$(DIST_DIR)/$(PACKAGE_NAME)/bin/"
	cp -R $(PACKAGE_FILES) "$(DIST_DIR)/$(PACKAGE_NAME)/"
	mv "$(DIST_DIR)/$(PACKAGE_NAME)/scripts/install-binary.sh" "$(DIST_DIR)/$(PACKAGE_NAME)/install.sh"
	rm -rf "$(DIST_DIR)/$(PACKAGE_NAME)/scripts"
	chmod 0755 "$(DIST_DIR)/$(PACKAGE_NAME)/install.sh"
	tar -czf "$(PACKAGE_ARCHIVE)" -C "$(DIST_DIR)" "$(PACKAGE_NAME)"
	rm -rf "$(DIST_DIR)/$(PACKAGE_NAME)"
	@echo "Created $(PACKAGE_ARCHIVE)"

release: check dist package

clean-dist:
	rm -rf "$(DIST_DIR)"

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(OBJ) $(TEST_SUPPORT_OBJ) $(TEST_OBJ) $(TARGET) $(TEST_TARGETS)
	rm -rf *.dSYM tests/*.dSYM

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean clean-dist run test test-asan fmt fmt-check analyze check dist package release install uninstall
