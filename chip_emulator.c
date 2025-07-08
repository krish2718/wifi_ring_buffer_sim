#include "shared.h"
#include <stdio.h>
#include <stdlib.h> // For rand()
#include <stdint.h> // For uintptr_t

// --- Simulated CHIP Internal State ---
static volatile uint32_t chip_tx_tail = 0; // Where CHIP reads from shared Tx buffer
static volatile uint32_t chip_rx_head = 0; // Where CHIP writes to shared Rx buffer

// Mock simulated memory for registers (defined in host.c)
extern uint32_t simulated_chip_registers[7];

// --- Simulated Interrupts ---
// Function to "raise" an interrupt to the HOST
void chip_raise_interrupt(uint32_t bit) {
    BUS_WRITE_REG(CHIP_REG_INT_STATUS, BUS_READ_REG(CHIP_REG_INT_STATUS) | bit);
    printf("CHIP_EMU: Raised interrupt 0x%x\n", bit);
}

// --- Emulator Initialization ---
void chip_emulator_init() {
    printf("CHIP_EMU: Initializing emulator...\n");
    // Ensure initial pointers match the hardware's reset state
    chip_tx_tail = 0;
    chip_rx_head = 0;
    // Set initial hardware-side pointers in the simulated registers for HOST to read
    BUS_WRITE_REG(CHIP_REG_TX_TAIL_PTR, chip_tx_tail);
    BUS_WRITE_REG(CHIP_REG_RX_HEAD_PTR, chip_rx_head);
    printf("CHIP_EMU: Emulator initialized.\n");
}

// --- Simulate CHIP's TX processing (reading from shared memory) ---
void chip_emulator_process_tx() {
    // CHIP reads HOST's published TX head pointer
    uint32_t host_tx_head_pub = BUS_READ_REG(CHIP_REG_HOST_TX_HEAD_PUB);

    // Calculate data available for CHIP to process
    uint32_t data_available;
    if (host_tx_head_pub >= chip_tx_tail) {
        data_available = host_tx_head_pub - chip_tx_tail;
    } else {
        data_available = TX_BUFFER_SIZE - chip_tx_tail + host_tx_head_pub;
    }

    if (data_available > 0) {
        // Invalidate cache for the data it's about to read (from HOST's writes)
        mock_dcache_invalidate_range((uintptr_t)tx_buffer_ptr, TX_BUFFER_SIZE); // Simplified for whole buffer
        DMB();

        // Simulate processing a packet
        // First, read the length header
        if (data_available < PACKET_LENGTH_FIELD_SIZE) {
            // Not enough for header, wait for more data
            return;
        }

        uint16_t packet_payload_len;
        uint32_t header_offset = chip_tx_tail;

        if ((header_offset + PACKET_LENGTH_FIELD_SIZE) > TX_BUFFER_SIZE) {
            uint8_t byte0 = tx_buffer_ptr[header_offset];
            uint8_t byte1 = tx_buffer_ptr[0];
            packet_payload_len = (uint16_t)byte0 | ((uint16_t)byte1 << 8);
        } else {
            packet_payload_len = *(uint16_t*)(tx_buffer_ptr + header_offset);
        }

        uint32_t total_packet_len = packet_payload_len + PACKET_LENGTH_FIELD_SIZE;

        if (data_available < total_packet_len) {
            // Not a full packet yet, wait
            return;
        }

        printf("CHIP_EMU_TX: Processing packet from HOST. Len: %u. First byte: 0x%02x\n",
               packet_payload_len, tx_buffer_ptr[(chip_tx_tail + PACKET_LENGTH_FIELD_SIZE) % TX_BUFFER_SIZE]);

        // Simulate internal CHIP processing and transmission
        // Advance CHIP's local Tx tail pointer
        chip_tx_tail = (chip_tx_tail + total_packet_len) % TX_BUFFER_SIZE;

        // Publish updated Tx tail pointer to HOST via simulated register
        DMB(); // Ensure data processing is conceptually complete
        BUS_WRITE_REG(CHIP_REG_TX_TAIL_PTR, chip_tx_tail);
        DSB();

        // If enough space is freed, raise TX_SPACE_AVAIL_BIT interrupt
        uint32_t space_freed;
        if (chip_tx_tail >= host_tx_head_pub) { // This comparison is for the HOST's current view
            space_freed = TX_BUFFER_SIZE - (chip_tx_tail - host_tx_head_pub);
        } else {
            space_freed = host_tx_head_pub - chip_tx_tail;
        }

        if (space_freed >= TX_LOW_WATERMARK_THRESHOLD) {
             chip_raise_interrupt(CHIP_INT_TX_SPACE_AVAIL_BIT);
        }

    }
}

// --- Simulate CHIP's RX generation (writing to shared memory) ---
void chip_emulator_generate_rx() {
    // CHIP reads HOST's published RX tail pointer
    uint32_t host_rx_tail_pub = BUS_READ_REG(CHIP_REG_HOST_RX_TAIL_PUB);

    // Calculate space available for CHIP to write
    uint32_t space_available;
    if (chip_rx_head >= host_rx_tail_pub) {
        space_available = RX_BUFFER_SIZE - (chip_rx_head - host_rx_tail_pub) - 1; // -1 to distinguish full/empty
    } else {
        space_available = (host_rx_tail_pub - chip_rx_head) - 1; // -1 to distinguish full/empty
    }

    // Simulate receiving a packet (e.g., random size)
    uint32_t simulated_payload_len = (rand() % 100) + 10; // Random length between 10 and 109 bytes
    uint32_t total_packet_len = simulated_payload_len + PACKET_LENGTH_FIELD_SIZE;

    if (space_available < total_packet_len) {
        // No space to write a full packet
        return;
    }

    // --- Write Length Header ---
    uint16_t len_header = (uint16_t)simulated_payload_len;
    uint32_t current_offset = chip_rx_head;

    *(uint16_t*)(rx_buffer_ptr + current_offset) = len_header;
    current_offset = (current_offset + PACKET_LENGTH_FIELD_SIZE) % RX_BUFFER_SIZE;

    // --- Write Packet Payload ---
    if ((current_offset + simulated_payload_len) > RX_BUFFER_SIZE) {
        uint32_t first_part_len = RX_BUFFER_SIZE - current_offset;
        // Fill with dummy data (simulate received CHIP data)
        for (uint32_t i = 0; i < first_part_len; i++) {
            rx_buffer_ptr[current_offset + i] = (uint8_t)(rand() % 256);
        }
        for (uint32_t i = 0; i < (simulated_payload_len - first_part_len); i++) {
            rx_buffer_ptr[i] = (uint8_t)(rand() % 256);
        }
    } else {
        for (uint32_t i = 0; i < simulated_payload_len; i++) {
            rx_buffer_ptr[current_offset + i] = (uint8_t)(rand() % 256);
        }
    }

    // Update CHIP's local Rx head pointer
    chip_rx_head = (chip_rx_head + total_packet_len) % RX_BUFFER_SIZE;

    // Ensure all writes to shared RAM are complete
    DMB();
    mock_dcache_clean_range((uintptr_t)rx_buffer_ptr + ((current_offset - (PACKET_LENGTH_FIELD_SIZE + simulated_payload_len) + RX_BUFFER_SIZE) % RX_BUFFER_SIZE), total_packet_len);

    // Publish updated Rx head pointer to HOST via simulated register
    BUS_WRITE_REG(CHIP_REG_RX_HEAD_PTR, chip_rx_head);
    DSB();

    printf("CHIP_EMU_RX: Generated packet. Len: %u. New Head: %u.\n", simulated_payload_len, chip_rx_head);

    // If enough data is available, raise RX_DATA_READY_BIT interrupt
    uint32_t data_written;
    if (chip_rx_head >= host_rx_tail_pub) { // This comparison is for HOST's last consumed position
        data_written = chip_rx_head - host_rx_tail_pub;
    } else {
        data_written = RX_BUFFER_SIZE - host_rx_tail_pub + chip_rx_head;
    }

    if (data_written >= RX_HIGH_WATERMARK_THRESHOLD) {
        chip_raise_interrupt(CHIP_INT_RX_DATA_READY_BIT);
    }
}

// --- Main Emulator Loop (simulates hardware's continuous operation) ---
void chip_emulator_run_cycle() {
    // In a real hardware IP, these would run concurrently and continuously.
    // In simulation, we call them sequentially.

    // Try to process outgoing (TX) data from HOST
    chip_emulator_process_tx();

    // Try to generate incoming (RX) data for HOST
    // Simulate some randomness for when RX data arrives
    if ((rand() % 10) < 5) { // 50% chance to generate RX data each cycle
        chip_emulator_generate_rx();
    }
}
