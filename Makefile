# Makefile for High Performance Design Simulation

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
CPPFLAGS = -DSIMULATION_MODE

# Target executable
TARGET = wifi_ring_buffer_sim

# Source files
SOURCES = host.c chip_emulator.c
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)

# Compile source files to object files
%.o: %.c shared.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Clean target - remove all generated files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Run target - build and execute
run: $(TARGET)
	./$(TARGET)

# Install target (if needed for deployment)
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Uninstall target
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the simulation executable (default)"
	@echo "  run       - Build and run the simulation"
	@echo "  clean     - Remove all generated files"
	@echo "  install   - Install executable to /usr/local/bin/"
	@echo "  uninstall - Remove installed executable"
	@echo "  help      - Show this help message"

# Phony targets
.PHONY: all clean run install uninstall help
