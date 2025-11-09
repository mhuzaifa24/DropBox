# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread -I./include -O0
TARGET = server
TEST_TARGET = test_client

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = test


# Source files
SRCS = $(SRC_DIR)/server.c \
       $(SRC_DIR)/client_queue.c \
       $(SRC_DIR)/task_queue.c \
       $(SRC_DIR)/client_pool.c \
       $(SRC_DIR)/worker_pool.c \
       $(SRC_DIR)/user_auth.c \
       $(SRC_DIR)/file_ops.c \
       $(SRC_DIR)/utils.c

TEST_SRCS = $(TEST_DIR)/test_client.c

# Object files
OBJS = $(SRCS:.c=.o)
TEST_OBJS = $(TEST_SRCS:.c=.o)

# Default target
all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) $(TEST_OBJS)

# Run with valgrind for memory checking
run-valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET) 8080

# Run with thread sanitizer
run-tsan: $(TARGET)
	$(CC) $(CFLAGS) -fsanitize=thread -o $(TARGET)-tsan $(SRCS)
	./$(TARGET)-tsan 8080

.PHONY: valgrind-test test-client

test-client:
	python3 test/test_client.py

valgrind-test: server
	@echo "Starting server under valgrind -> valgrind-server.log"
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--num-callers=20 --log-file=valgrind-server.log ./server 8080 & \
	PID=$$!; sleep 0.5; python3 test/test_client.py; sleep 0.5; kill -INT $$PID || true; wait $$PID || true; \
	echo "Valgrind test finished. Inspect valgrind-server.log"


