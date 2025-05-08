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

# Define the executable target for process_fairness
SLEEP_WAKE_PROCESS_EXE = $(BIN_DIR)/sleep_wake_process

# Define the executable target for process_fairness
SLEEP_WAKE_THREAD_EXE = $(BIN_DIR)/sleep_wake_thread

DETERMINISTIC_LATENCY_EXE = $(BIN_DIR)/deterministic_latency

IPC_LATENCY_EXE = $(BIN_DIR)/ipc_latency

IPC_MQ_LATENCY_EXE = $(BIN_DIR)/ipc_mq_latency

PERIODIC_CONTROL_SIM_EXE = $(BIN_DIR)/periodic_control_sim

# Default target builds object files and links the executable
all: $(BIN_DIR) $(OBJ_SUBDIRS) $(OBJ) $(THREAD_FAIRNESS_EXE) $(PROCESS_FAIRNESS_EXE) $(SLEEP_WAKE_PROCESS_EXE) \
		$(SLEEP_WAKE_THREAD_EXE) $(DETERMINISTIC_LATENCY_EXE) $(IPC_LATENCY_EXE) $(IPC_MQ_LATENCY_EXE) $(PERIODIC_CONTROL_SIM_EXE)

# Create required directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)
$(OBJ_SUBDIRS):
	mkdir -p $@

# Compile all .c files into their respective subdirectories
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_SUBDIRS)
	$(GCC) $(CFLAGS) $(INCLUDE_DIRS) -MMD -c $< -o $@

# Linking rule for thread_fairness executable
$(THREAD_FAIRNESS_EXE): $(OBJ_DIR)/scheduling/threads/thread_fairness.o
	$(GCC) $(CFLAGS) $^ -o $@

# Linking rule for process_fairness executable
$(PROCESS_FAIRNESS_EXE): $(OBJ_DIR)/scheduling/process/process_fairness.o
	$(GCC) $(CFLAGS) $^ -o $@

# Linking rule for process_fairness executable
$(SLEEP_WAKE_PROCESS_EXE): $(OBJ_DIR)/scheduling/process/sleep_wake_process.o
	$(GCC) $(CFLAGS) $^ -o $@

# Linking rule for process_fairness executable
$(SLEEP_WAKE_THREAD_EXE): $(OBJ_DIR)/scheduling/threads/sleep_wake_thread.o
	$(GCC) $(CFLAGS) $^ -o $@

$(DETERMINISTIC_LATENCY_EXE): $(OBJ_DIR)/scheduling/latency/deterministic_latency.o
	$(GCC) $(CFLAGS) $^ -o $@

$(IPC_LATENCY_EXE): $(OBJ_DIR)/ipc/latency/ipc_latency.o
	$(GCC) $(CFLAGS) $^ -o $@

$(IPC_MQ_LATENCY_EXE): $(OBJ_DIR)/ipc/latency/ipc_mq_latency.o
	$(GCC) $(CFLAGS) $^ -o $@

$(PERIODIC_CONTROL_SIM_EXE): $(OBJ_DIR)/periodic_control_sim.o
	$(GCC) $(CFLAGS) $^ -o $@

# Include dependency files
-include $(OBJ:.o=.d)

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*

.PHONY: all clean
