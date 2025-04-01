# Compiler and Flags
GCC = gcc
CFLAGS = -lpthread -lc

# Directories
SRC_DIR = $(CURDIR)/src
INCLUDE_DIR = $(CURDIR)/include
OBJ_DIR = $(CURDIR)/build
BIN_DIR = $(CURDIR)/bin

# Find all the header directories inside include/
INCLUDE_DIRS = $(shell find $(INCLUDE_DIR) -mindepth 1 -type d | xargs -I {} echo -I{})

# Generate list of build subdirectories to mirror the src/
OBJ_SUBDIRS = $(shell find $(SRC_DIR) -type d | sed 's|$(SRC_DIR)|$(OBJ_DIR)|')

# Source files
SRC = $(shell find $(SRC_DIR) -name '*.c')

# Object files (preserve subdirectories)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Define the executable target for thread_fairness
THREAD_FAIRNESS_EXE = $(BIN_DIR)/thread_fairness

# Define the executable target for process_fairness
PROCESS_FAIRNESS_EXE = $(BIN_DIR)/process_fairness

# Default target builds object files and links the executable
all: $(BIN_DIR) $(OBJ_SUBDIRS) $(OBJ) $(THREAD_FAIRNESS_EXE) $(PROCESS_FAIRNESS_EXE)

# Create required directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)
$(OBJ_SUBDIRS):
	mkdir -p $@

# Compile all .c files into their respective subdirectories
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_SUBDIRS)
	$(GCC) $(CFLAGS) $(INCLUDE_DIRS) -MMD -c $< -o $@

# Linking rule for thread_fairness executable
$(THREAD_FAIRNESS_EXE): $(OBJ_DIR)/scheduling/pthreads/thread_fairness.o
	$(GCC) $(CFLAGS) $^ -o $@

# Linking rule for process_fairness executable
$(PROCESS_FAIRNESS_EXE): $(OBJ_DIR)/scheduling/minix3/process_fairness.o
	$(GCC) $(CFLAGS) $^ -o $@

# Include dependency files
-include $(OBJ:.o=.d)

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*

.PHONY: all clean
