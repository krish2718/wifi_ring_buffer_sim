#include "shared.h"
#include <stdio.h> // For printf (debug purposes)
#include <stdlib.h> // For rand(), srand()
#include <stdint.h> // For uintptr_t

// --- HOST Local Ring Buffer Pointers ---
static volatile uint32_t host_tx_head = 0; // Where HOST will write next
static volatile uint32_t host_rx_tail = 0; // Where HOST last read from

// Pointers to the shared memory regions (these will be part of a global simulated memory array)
// For `main`, you'd allocate memory and assign these.
uint8_t * tx_buffer_ptr = (uint8_t *)TX_BUFFER_START_ADDR;
uint8_t * rx_buffer_ptr = (uint8_t *)RX_BUFFER_START_ADDR;

// Mock simulated memory for registers (declared extern in shared.h)
// This array represents the memory-mapped registers of the CHIP accessible via BUS.
uint32_t simulated_chip_registers[7]; // Size matches the number of registers defined

// Mock simulated shared RAM. In a real system, this would be actual DRAM.
// For simulation, we'll create a single large array.
// Total shared memory: TX_BUFFER_SIZE + RX_BUFFER_SIZE
#define TOTAL_SHARED_MEMORY_SIZE (TX_BUFFER_SIZE + RX_BUFFER_SIZE)
uint8_t simulated_shared_ram[TOTAL_SHARED_MEMORY_SIZE];


// --- Mock Cache Maintenance Functions for SIMULATION_MODE ---
void mock_dcache_clean_range(uint32_t addr __attribute__((unused)), uint32_t len __attribute__((unused))) {
    // In a real system, this would call your SoC's D-Cache API.
    // In simulation, it's just a print or a no-op if memory is directly accessed.
    // printf("DEBUG: D-Cache Clean: 0x%lx, Len: %lu\n", addr, len);
}

void mock_dcache_invalidate_range(uint32_t addr __attribute__((unused)), uint32_t len __attribute__((unused))) {
    // In a real system, this would call your SoC's D-Cache API.
    // In simulation, it's just a print or a no-op if memory is directly accessed.
    // printf("DEBUG: D-Cache Invalidate: 0x%lx, Len: %lu\n", addr, len);
}


// --- HOST Initialization ---
void host_chip_driver_init() {
    printf("HOST: Initializing CHIP driver...\n");

    // Initialize local pointers
    host_tx_head = 0;
    host_rx_tail = 0;

    // Zero-out simulated registers
    for (int i = 0; i < 7; i++) {
        simulated_chip_registers[i] = 0;
    }

    // Clear any pending interrupts on the CHIP side
    BUS_WRITE_REG(CHIP_REG_INT_CLEAR, 0xFFFFFFFFUL);

    // Publish initial HOST pointers to the CHIP.
    BUS_WRITE_REG(CHIP_REG_HOST_TX_HEAD_PUB, host_tx_head);
    BUS_WRITE_REG(CHIP_REG_HOST_RX_TAIL_PUB, host_rx_tail);

    // Ensure all writes are completed and visible to the CHIP over BUS.
    DSB();
    ISB();

    // Enable specific interrupts from the CHIP
    BUS_WRITE_REG(CHIP_REG_INT_ENABLE,
                  CHIP_INT_RX_DATA_READY_BIT |
                  CHIP_INT_TX_SPACE_AVAIL_BIT |
                  CHIP_INT_ERROR_BIT);
    printf("HOST: CHIP driver initialized. Pointers published.\n");
}

// --- HOST Transmit Function ---
// Returns 0 on success, <0 on error
int host_chip_send_packet(const uint8_t *data, uint32_t len) {
    // Total size to write: packet data + length header
    uint32_t total_write_len = len + PACKET_LENGTH_FIELD_SIZE;

    if (total_write_len > TX_BUFFER_SIZE) {
        printf("HOST_TX_ERR: Packet too large (%u bytes) for buffer size %lu.\n", total_write_len, TX_BUFFER_SIZE);
        return -1; // Packet too large
    }

    // Read the CHIP's current Tx consumption pointer (tail)
    uint32_t chip_tx_tail = BUS_READ_REG(CHIP_REG_TX_TAIL_PTR);

    // Calculate available space in the ring buffer
    uint32_t space_available;
    if (host_tx_head >= chip_tx_tail) {
        space_available = TX_BUFFER_SIZE - (host_tx_head - chip_tx_tail) - 1; // -1 to distinguish full from empty
    } else {
        space_available = (chip_tx_tail - host_tx_head) - 1; // -1 to distinguish full from empty
    }

    if (space_available < total_write_len) {
        printf("HOST_TX_ERR: Not enough space in Tx buffer. Avail: %u, Needed: %u.\n", space_available, total_write_len);
        return -2; // Not enough space
    }

    // --- Write Length Header ---
    uint16_t len_header = (uint16_t)len; // Actual payload length
    uint32_t current_offset = host_tx_head;

    // Write length header (2 bytes) - Assuming it fits without wrapping for 2 bytes
    *(uint16_t*)(tx_buffer_ptr + current_offset) = len_header;
    current_offset = (current_offset + PACKET_LENGTH_FIELD_SIZE) % TX_BUFFER_SIZE;

    // --- Copy Packet Data ---
    if ((current_offset + len) > TX_BUFFER_SIZE) {
        // Packet data wraps around
        uint32_t first_part_len = TX_BUFFER_SIZE - current_offset;
        memcpy(tx_buffer_ptr + current_offset, data, first_part_len);
        memcpy(tx_buffer_ptr, data + first_part_len, len - first_part_len);
    } else {
        // Packet data fits in a single contiguous block
        memcpy(tx_buffer_ptr + current_offset, data, len);
    }

    // Update local head pointer
    host_tx_head = (host_tx_head + total_write_len) % TX_BUFFER_SIZE;

    // Ensure all data writes to shared RAM are complete before updating the public pointer.
    DMB();
    mock_dcache_clean_range((uintptr_t)tx_buffer_ptr + ((current_offset - (PACKET_LENGTH_FIELD_SIZE + len) + TX_BUFFER_SIZE) % TX_BUFFER_SIZE), total_write_len);

    // Publish the updated HOST Tx head pointer to the CHIP
    BUS_WRITE_REG(CHIP_REG_HOST_TX_HEAD_PUB, host_tx_head);

    // Ensure the pointer update is visible to CHIP (via BUS)
    DSB();
    ISB();

    printf("HOST_TX: Packet sent. Len: %u. New Head: %u.\n", len, host_tx_head);
    return 0; // Success
}

// Forward declaration
void host_chip_process_received_data(void);

// --- HOST Receive Interrupt Handler ---
void host_chip_irq_handler() {
    uint32_t int_status = BUS_READ_REG(CHIP_REG_INT_STATUS);

    // Process Rx Data Ready interrupt
    if (int_status & CHIP_INT_RX_DATA_READY_BIT) {
        BUS_WRITE_REG(CHIP_REG_INT_CLEAR, CHIP_INT_RX_DATA_READY_BIT);
        printf("HOST_RX_ISR: RX Data Ready Interrupt.\n");
        host_chip_process_received_data();
    }

    // Process Tx Space Available interrupt (optional)
    if (int_status & CHIP_INT_TX_SPACE_AVAIL_BIT) {
        BUS_WRITE_REG(CHIP_REG_INT_CLEAR, CHIP_INT_TX_SPACE_AVAIL_BIT);
        printf("HOST_TX_ISR: TX Space Available Interrupt.\n");
    }

    // Process Error interrupt
    if (int_status & CHIP_INT_ERROR_BIT) {
        BUS_WRITE_REG(CHIP_REG_INT_CLEAR, CHIP_INT_ERROR_BIT);
        printf("HOST_ERR_ISR: CHIP Error Interrupt! Status: 0x%x\n", int_status);
    }
}

// --- HOST Receive Processing Function ---
void host_chip_process_received_data() {
    uint32_t current_rx_tail = host_rx_tail;
    uint32_t chip_rx_head = BUS_READ_REG(CHIP_REG_RX_HEAD_PTR); // Get CHIP's current written position

    // Invalidate D-Cache for the potential new data in the Rx buffer.
    mock_dcache_invalidate_range((uintptr_t)rx_buffer_ptr, RX_BUFFER_SIZE);
    DMB(); // Ensure invalidate completes before memory access

    while (current_rx_tail != chip_rx_head) {
        uint32_t bytes_available;
        if (chip_rx_head >= current_rx_tail) {
            bytes_available = chip_rx_head - current_rx_tail;
        } else {
            bytes_available = RX_BUFFER_SIZE - current_rx_tail + chip_rx_head;
        }

        if (bytes_available < PACKET_LENGTH_FIELD_SIZE) {
            printf("HOST_RX: Not enough for header. Avail: %u.\n", bytes_available);
            break;
        }

        // --- Read Packet Length Header ---
        uint16_t packet_payload_len;
        uint32_t header_offset = current_rx_tail;

        if ((header_offset + PACKET_LENGTH_FIELD_SIZE) > RX_BUFFER_SIZE) {
            uint8_t byte0 = rx_buffer_ptr[header_offset];
            uint8_t byte1 = rx_buffer_ptr[0];
            packet_payload_len = (uint16_t)byte0 | ((uint16_t)byte1 << 8); // Assuming little-endian
        } else {
            packet_payload_len = *(uint16_t*)(rx_buffer_ptr + header_offset);
        }

        uint32_t total_packet_len = packet_payload_len + PACKET_LENGTH_FIELD_SIZE;

        if (bytes_available < total_packet_len) {
            printf("HOST_RX: Partial packet. Avail: %u, Needed: %u. Waiting...\n", bytes_available, total_packet_len);
            break;
        }

        // --- Process Packet Payload ---
        uint8_t *packet_start_data_ptr = rx_buffer_ptr + ((current_rx_tail + PACKET_LENGTH_FIELD_SIZE) % RX_BUFFER_SIZE);

        printf("HOST_RX: Received Packet! Payload Len: %u. Data Start Offset: %lu. (First byte: 0x%02x)\n",
               packet_payload_len, (current_rx_tail + PACKET_LENGTH_FIELD_SIZE) % RX_BUFFER_SIZE, *packet_start_data_ptr);

        // Here, pass the packet to the higher-level networking stack
        // e.g., network_stack_receive(packet_start_data_ptr, packet_payload_len);

        // Update local tail pointer to mark this packet as consumed
        current_rx_tail = (current_rx_tail + total_packet_len) % RX_BUFFER_SIZE;

        // Update CHIP's head for the next loop iteration (in case it wrote more data)
        chip_rx_head = BUS_READ_REG(CHIP_REG_RX_HEAD_PTR);
    }

    // Publish the updated HOST Rx tail pointer to the CHIP
    DMB();
    BUS_WRITE_REG(CHIP_REG_HOST_RX_TAIL_PUB, current_rx_tail);
    DSB();
    ISB();

    host_rx_tail = current_rx_tail; // Update global HOST tail
    printf("HOST_RX: Finished processing. New Tail: %u.\n", host_rx_tail);
}

// --- Main HOST Application Loop (for simulation) ---
// In a real embedded system, this would be main(), possibly with an RTOS.
// For simulation, we integrate it with the emulator.
// extern void chip_emulator_run_cycle(); // Declared in chip_emulator.c
extern void chip_emulator_init();
extern void chip_emulator_run_cycle();

void host_main_loop() {
    host_chip_driver_init();
    chip_emulator_init(); // Initialize the emulator

    printf("\n--- HOST and CHIP Simulation Start ---\n");

    // Simulate HOST sending a few packets
    uint8_t test_packet_tx1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x02, 0x03, 0x04};
    host_chip_send_packet(test_packet_tx1, sizeof(test_packet_tx1));

    uint8_t test_packet_tx2[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xA0, 0xB0};
    host_chip_send_packet(test_packet_tx2, sizeof(test_packet_tx2));

    // Simulate a few hundred "cycles" where both HOST and CHIP might run
    for (int cycle = 0; cycle < 50; cycle++) {
        printf("\n--- Simulation Cycle %d ---\n", cycle);

        // HOST checks for incoming data (via IRQ or polling in simpler designs)
        // In this simulation, we'll manually check and call the handler.
        host_chip_irq_handler();

        // Simulate CHIP's internal hardware operations (TX processing, RX generation)
        chip_emulator_run_cycle();

        // HOST can try to send more if space becomes available
        if (cycle % 10 == 0) { // Every 10 cycles, try to send another packet
             uint8_t dynamic_packet[20];
             for(int i = 0; i < 20; i++) dynamic_packet[i] = (uint8_t)(0xDA + i);
             host_chip_send_packet(dynamic_packet, sizeof(dynamic_packet));
        }
    }

    printf("\n--- Simulation End ---\n");
}

int main() {
    // Initialize the simulated shared RAM (equivalent to main memory)
    memset(simulated_shared_ram, 0, TOTAL_SHARED_MEMORY_SIZE);

    // Assign global pointers to the correct offsets within the simulated_shared_ram
    // This is how the BUS_READ_REG/WRITE_REG macros, tx_buffer_ptr, rx_buffer_ptr
    // will actually access the simulated memory.
    *(uint8_t**)(&tx_buffer_ptr) = simulated_shared_ram + (TX_BUFFER_START_ADDR - SHARED_RAM_BASE_ADDR);
    *(uint8_t**)(&rx_buffer_ptr) = simulated_shared_ram + (RX_BUFFER_START_ADDR - SHARED_RAM_BASE_ADDR);


    host_main_loop();

    return 0;
}
