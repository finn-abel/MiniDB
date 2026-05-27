CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g
INCLUDES = -Iinclude

TARGET = MiniDB

SRC = src/main.c src/db.c src/value.c
OBJ = $(SRC:.c=.o)

TEST_TARGETS = test_value
TEST_OBJ = tests/test_value.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test_value: tests/test_value.o src/value.o
	$(CC) $(CFLAGS) $(INCLUDES) -o test_value tests/test_value.o src/value.o

test: test_value
	./test_value

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_OBJ) $(TEST_TARGETS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run test
