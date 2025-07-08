# High Performance Design Simulation

A simulation framework for high-performance embedded systems featuring HOST processor and CHIP IP communication over BUS interface.

## Overview

This project simulates a high-performance embedded system with:
- **HOST Processor**: Generic host processor handling CHIP driver operations
- **CHIP IP**: Hardware IP block for communication
- **BUS Interface**: High-speed interconnect between processor and IP blocks
- **Ring Buffer Communication**: Efficient data transfer using shared memory ring buffers

## Architecture

```
┌─────────────┐     BUS     ┌─────────────┐
│    HOST     │◄──────────►│    CHIP     │
│  Processor  │             │     IP      │
└─────────────┘             └─────────────┘
       │                           │
       │                   ┌─────────────┐
       └──────────────────►│ Shared RAM  │
                           │ Ring Buffers│
                           └─────────────┘
```

### Key Components

- **HOST Driver**: Handles packet transmission and reception
- **CHIP IP Emulator**: Simulates hardware IP behavior
- **Shared Memory**: Ring buffers for TX/RX data transfer
- **Interrupt System**: Event-driven communication

## Building

### Prerequisites

- GCC compiler with C11 support
- Make build system
- Linux environment (tested on Linux 6.11.0-29-generic)

### Compilation

```bash
# Build the simulation executable
make

# Or build and run directly
make run

# Clean build artifacts
make clean
```

### Available Make Targets

- `make` or `make all` - Build the simulation executable
- `make run` - Build and run the simulation
- `make clean` - Remove all generated files
- `make install` - Install executable to system
- `make uninstall` - Remove from system
- `make help` - Show available targets

## Usage

### Running the Simulation

```bash
# Build and run
make run

# Or build first, then run
make
./wifi_ring_buffer_sim
```

### Simulation Output

The simulation demonstrates:
- HOST sending test packets to CHIP
- CHIP processing and responding
- Ring buffer management
- Interrupt handling
- Memory synchronization

Example output:
```
HOST: Initializing CHIP driver...
HOST: CHIP driver initialized. Pointers published.
HOST_TX: Packet sent. Len: 10. New Head: 12.
HOST_TX: Packet sent. Len: 12. New Head: 26.
--- Simulation Cycle 0 ---
HOST_RX_ISR: RX Data Ready Interrupt.
HOST_RX: Received Packet! Payload Len: 10. Data Start Offset: 0.
```

## Project Structure

```
high_perf_design/
├── host.c                 # HOST processor simulation
├── chip_emulator.c        # CHIP IP hardware emulator
├── shared.h               # Shared definitions and macros
├── Makefile               # Build configuration
├── README.md              # This file
└── wifi_ring_buffer_sim   # Compiled executable
```

## Key Features

### Ring Buffer Communication
- **TX Buffer**: CM33 → Wi-Fi IP data transfer
- **RX Buffer**: Wi-Fi IP → CM33 data transfer
- **Atomic Operations**: Thread-safe pointer updates
- **Cache Management**: Proper memory synchronization

### Interrupt System
- **RX Data Ready**: New data available for processing
- **TX Space Available**: Buffer space freed for transmission
- **Error Handling**: Hardware error detection and reporting

### Memory Management
- **Shared RAM**: Simulated memory-mapped regions
- **Register Access**: Bus based register read/write
- **Cache Coherency**: D-Cache clean/invalidate operations

## Technical Details

### Buffer Sizes
- TX Buffer: 1024 bytes
- RX Buffer: 1024 bytes
- Packet Length Field: 2 bytes

### Register Map
- `CHIP_REG_HOST_TX_HEAD_PUB`: Published TX head pointer
- `CHIP_REG_HOST_RX_TAIL_PUB`: Published RX tail pointer
- `CHIP_REG_TX_TAIL_PTR`: CHIP TX consumption pointer
- `CHIP_REG_RX_HEAD_PTR`: CHIP RX production pointer
- `CHIP_REG_INT_STATUS`: Interrupt status register
- `CHIP_REG_INT_ENABLE`: Interrupt enable register
- `CHIP_REG_INT_CLEAR`: Interrupt clear register

### Synchronization
- **DMB**: Data Memory Barrier for write completion
- **DSB**: Data Synchronization Barrier for BUS visibility
- **ISB**: Instruction Synchronization Barrier for ordering

## Development

### Adding New Features
1. Modify `host.c` for HOST-side changes
2. Update `chip_emulator.c` for IP-side changes
3. Update `shared.h` for shared definitions
4. Test with `make run`

### Debugging
- Enable debug prints by uncommenting printf statements
- Use `-g` flag for GDB debugging
- Check ring buffer state in simulation output

## License

This project follows open source development practices similar to Linux kernel and Zephyr RTOS.

## Contributing

1. Follow the existing code style
2. Add appropriate comments and documentation
3. Test changes with `make run`
4. Update this README for significant changes

## Troubleshooting

### Common Issues

**Build Errors:**
```bash
# Ensure GCC is installed
sudo apt-get install build-essential

# Check C11 support
gcc --version
```

**Runtime Errors:**
- Verify all source files are present
- Check file permissions on executable
- Ensure sufficient system memory

**Simulation Issues:**
- Review ring buffer pointer logic
- Check interrupt handling
- Verify memory synchronization

For additional support, review the source code comments and simulation output for detailed debugging information. 