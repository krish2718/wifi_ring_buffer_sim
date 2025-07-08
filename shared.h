#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>
#include <string.h> // For memcpy

// --- Shared Memory & Ring Buffer Definitions ---

// Base address of the shared RAM region (Adjust based on your SoC memory map)
#define SHARED_RAM_BASE_ADDR        0x20000000UL // Using UL for unsigned long

// Size of the ring buffers (must be power of 2 for easy modulo arithmetic)
// These sizes impact performance vs. memory footprint. Tune based on needs.
#define TX_BUFFER_SIZE              (4096UL) // Example: 4KB
#define RX_BUFFER_SIZE              (4096UL) // Example: 4KB

// Pointers to the start of the ring buffers within shared RAM
#define TX_BUFFER_START_ADDR        (SHARED_RAM_BASE_ADDR)
#define RX_BUFFER_START_ADDR        (SHARED_RAM_BASE_ADDR + TX_BUFFER_SIZE)

// A minimum amount of space/data required to trigger an operation (e.g., DMA)
// This helps prevent excessive small transfers.
#define TX_LOW_WATERMARK_THRESHOLD  (TX_BUFFER_SIZE / 4) // Example: refill when 1/4 full
#define RX_HIGH_WATERMARK_THRESHOLD (RX_BUFFER_SIZE / 4) // Example: process when 1/4 full

// --- CHIP Register Addresses (Conceptual BUS-mapped) ---
// These addresses would be defined by the hardware team integrating the CHIP IP.
// Replace with actual addresses from your SoC's memory map.
#define CHIP_BASE_ADDR              0x50000000UL

// Registers for HOST to read CHIP's pointer status
#define CHIP_REG_TX_TAIL_PTR        (CHIP_BASE_ADDR + 0x00) // CHIP's current Tx consumption pointer
#define CHIP_REG_RX_HEAD_PTR        (CHIP_BASE_ADDR + 0x04) // CHIP's current Rx production pointer

// Registers for HOST to write its pointer updates for CHIP to read
#define CHIP_REG_HOST_TX_HEAD_PUB   (CHIP_BASE_ADDR + 0x08) // HOST's current Tx production pointer
#define CHIP_REG_HOST_RX_TAIL_PUB   (CHIP_BASE_ADDR + 0x0C) // HOST's current Rx consumption pointer

// Interrupt related registers
#define CHIP_REG_INT_STATUS         (CHIP_BASE_ADDR + 0x10) // Read current interrupt status
#define CHIP_REG_INT_CLEAR          (CHIP_BASE_ADDR + 0x14) // Write to clear interrupts
#define CHIP_REG_INT_ENABLE         (CHIP_BASE_ADDR + 0x18) // Write to enable/disable interrupts

// Define specific interrupt bits (example)
#define CHIP_INT_RX_DATA_READY_BIT  (1U << 0)
#define CHIP_INT_TX_SPACE_AVAIL_BIT (1U << 1)
#define CHIP_INT_ERROR_BIT          (1U << 2)

// Generic BUS memory-mapped register access macros
// In a real project, these might be wrapper functions provided by an SoC HAL.
// For simulation, these will simply access global variables that simulate memory-mapped registers.
#ifdef SIMULATION_MODE
// ...
extern uint8_t *tx_buffer_ptr; // Declare as extern
extern uint8_t *rx_buffer_ptr; // Declare as extern

// In simulation mode, use a simulated memory-mapped register block
// This needs to be defined in both host.c and chip_emulator.c
extern uint32_t simulated_chip_registers[7]; // 7 registers as defined above

#define BUS_READ_REG(addr)          (simulated_chip_registers[((addr) - CHIP_BASE_ADDR) / 4])
#define BUS_WRITE_REG(addr, val)    (simulated_chip_registers[((addr) - CHIP_BASE_ADDR) / 4] = (val))
#else
#define BUS_READ_REG(addr)          (*(volatile uint32_t *)(addr))
#define BUS_WRITE_REG(addr, val)    (*(volatile uint32_t *)(addr) = (val))
#endif


// --- Cache Coherency / Memory Barrier Macros ---
// In a simulation, these might not do anything unless you have a memory model.
#ifndef SIMULATION_MODE
#ifdef __ARM_ARCH_7M__ // Or specific for HOST like __HOST_ARCH__ if available
#define DMB() __asm volatile ("dmb" ::: "memory") // Data Memory Barrier
#define DSB() __asm volatile ("dsb" ::: "memory") // Data Synchronization Barrier
#define ISB() __asm volatile ("isb" ::: "memory") // Instruction Synchronization Barrier
#else
#define DMB() do {} while (0)
#define DSB() do {} while (0)
#define ISB() do {} while (0)
#endif // __ARM_ARCH_7M__
#else // SIMULATION_MODE
#define DMB() do {} while (0)
#define DSB() do {} while (0)
#define ISB() do {} while (0)
// Mock cache functions for simulation
extern void mock_dcache_clean_range(uint32_t addr, uint32_t len);
extern void mock_dcache_invalidate_range(uint32_t addr, uint32_t len);
#endif // SIMULATION_MODE


// --- Packet Framing Assumptions ---
#define PACKET_LENGTH_FIELD_SIZE    2 // Bytes

#endif // SHARED_H
