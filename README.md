# STM32 SPI Driver

Register-level SPI driver for the STM32L476RG implemented entirely without HAL.

## Project Goal

Rather than relying on STM32 HAL, this project develops an SPI peripheral driver directly from the reference manual.

My emphasis is on:

- Register-level programming
- Peripheral configuration
- Clock management
- GPIO alternate functions
- Protocol timing
- Interrupt handling
- DMA integration
- Debugging using hardware tools



## Hardware

MCU:
- STM32L476RG

Development Board:
- STM32 Nucleo-64

Debugger:
- ST-Link V2

Test Device:
- First: SPI Loopback
or
- Later: SPI Flash Memory

Clock
- 4 MHz System Clock

Tools

- Logic Analyzer






## Development Timeline

The driver was intentionally developed in stages.

| Stage | Status | Purpose |
|--------|--------|---------|
| Blocking Driver | Complete | Understand SPI registers |
| Interrupt Driver | In Progress | Eliminate CPU busy waiting |
| DMA Driver | Planned | High throughput transfers |



## Blocking Transmission High-Level Walkthrough

1. Call SPI_SEND_BYTE(void)
2. Drop CS line
3. Loop x times, x being the number of bytes we want to send
   -  Poll until TX buffer is empty -> previously written byte sent to shift register
   -  Once empty, send next byte into TX buffer -> let hardware send it over MOSI line
   -  Poll until RX buffer is not empty (contains eight bits) -> received from MISO line
   -  Once it contains one byte, throw it into "received" array
4. Wait for BSY status flag to turn off -> SPI cleans up to the default state
5. Raise CS line and return

## Blocking Data Flow

    Byte in global memory -> Once TXE = 1, byte sent to DR (TX FIFO Buffer) -> Since we cast to uint8_t, TXE gets set to 0 -> Once Shift Register Is Ready, Hardware byte into shift register
                                                                                                                                                                          |                                                                                                                                                                                                                                                      |
                                                                                                                                                                          |
                                                                                                                                                                          |
                                                                                                                                                                          V
    RX Buffer Set to Flag RXNE = 0 at eight bits instead of the default 16 out of 32 bits <----------Received byte gets sent to RX FIFO Buffer <-------- Hardware handles shifting out over MOSI and shifting in over MISO
      |                                                                                                                                                                                                                                                      |
      |
      |                                                                                                                                                                                                      
      V
    Read DR to pop byte off of RX Buffer


## Logic Analyzer Waveforms

This SPI waveform was captured using an eight-channel logic analyzer to observe and analyze:

- Clock frequency
- Clock polarity
- Clock phase
- Bit ordering
- Chip Select timing
- Timing Profiling / Throughput
    

<img width="1248" height="633" alt="Screenshot 2026-07-15 at 9 56 08 PM" src="https://github.com/user-attachments/assets/72d9dbd9-d31c-4a81-91a7-402fff620793" />
Here is the waveform for the completed baseline implementation of a polling-driven SPI master driver transmitting a four-byte payload(0x24, 0x48, 0x11, 0x54) from a uint8_t array.
By polling the TXE and RXNE status flags, we achieved zero inter-byte delay, maximizing throughput, however this design ensures the processor is stuck in instruction loops for the enire duration of the physical bus transaction.
This is very inefficient as the CPU spends the entire time polling registers in the function, rather than tending to other tasks or entering sleep mode.

**Key Performance Metrics:**

Physical Bus:
SPI Bus Clock Frequency: 250 kHz (4.00 μs per clock cycle, 32 μs serialization time per byte)
Active Frame Duration: 141.50 μs (from CS falling edge to CS rising edge)
Bus Efficiency: 100% bus utilization; by sending the BSY flag poll after the byte transmission while loop, the hardware pipeline maintained 0 μs of inter-byte idle time, achieving the theoretically maximum throughput at this SPI bus clock frequency
CS-to-SCK Setup Time: 11.5 μs setup time from CS dropping to first clock edge

Software Overhead:
Total Function Execution Time: 150.50 μs measured with GPIO toggle, from time of exiting main() to re-entry to main()
CPU Blocking Overhead: 100%, the CPU is blocked from other execution for the entire 150.50 μs of this four-byte transfer
Driver API Software Overhead: 11 μs due to several vital operations such as the function prologue + epilogue, pointer arithmetic, and GPIOA writes for setting the CS.  This is detailed below.

Latency Bottlenecks:
As for throughput, once the first bit gets sents / receieved, the SPI bus usage is at 100% exactly, as it 
takes 128 μs to complete all 32 clock cycles.  The real bottleneck right now in terms of speed other than SPI Clock Frequency 
is the latency of transitionary periods, which there are four of.

1. Function Call to CS Line Low latency: 5.50 - 6.00 μs
2. CS Line Low to First SPI Clock Edge Latency: 11.00 - 11.50 μs
3. End of Last Clock Cycle to CS Line High Latency: 0.00 μs
4. CS Line High to Return to Main Latency: 2.50 - 3.00 μs

## Function Call to CS Line Low latency

#### Latency: 5.50 - 6.00 μs

#### Latency Cause: BL + Function Epilogue + Setting incoming\_idx and outgoing\_idx to zero

#### Dissasembly:

08000216:   bl      0x8000250 <SPI_SEND_BYTE_POLLING>

First lines inside the function....

08000250:   push    {r7}
08000252:   add     r7, sp, #0
 51       	incoming\_idx = 0;
08000254:   ldr     r3, [pc, #156]  @ (0x80002f4 <SPI_SEND_BYTE_POLLING+164>)
08000256:   movs    r2, #0
08000258:   strb    r2, [r3, #0]
 52       	outgoing\_idx = 0;
0800025a:   ldr     r3, [pc, #156]  @ (0x80002f8 <SPI_SEND_BYTE_POLLING+168>)
0800025c:   movs    r2, #0
0800025e:   strb    r2, [r3, #0]

Looking at the Cortex-M4 Technical Reference Manual, we can see the number of clock cycles that an instruction from the 
ARMv7-M Thumb instruction set takes to execute.

Total: 4 clock cycles
Due to the 32-bit instruction that actually spans across two words, and the fact this call is a branching instruction, we 
can say this takes roughly 3 to 4 clock cycles.

Total: 1 clock cycles
Pushing the single register r7 onto the stack takes a flat 1 clock cycle.

Total: 10 clock cycles
Now, setting the two variables to zero takes a load (ldr) which is PC relative, so that is 2 clock cycles.
Moving a literal into a register is just 1 clock cycle.
Lastly, storing a byte is 2 clock cycles.


This comes out to about 15 clock cycles.  Taking a look at this it is clear that the biggest latency bottleneck here 
by a long shot is the variable setting.  Reducing those two variable write times as well as removing function call overhead 
could significantly reduce function call to CS-Low latency.
 

### Waveform Progression

<img width="1443" height="723" alt="Screenshot 2026-07-06 at 4 38 49 PM" src="https://github.com/user-attachments/assets/07e1fef4-ecf4-41c4-9064-fb66adca717a" />
Fig 1.1: Initial waveform Sending Dummy Byte (0x24) Over MOSI Line
Issue to fix: Right now I am using a for loop to handle delays between byte transmissions, delay is too large, implement an accurate time sensitive way to regulate delays

<img width="1251" height="636" alt="Screenshot 2026-07-06 at 5 33 46 PM" src="https://github.com/user-attachments/assets/32ed1b3a-a7dc-4b31-9fa9-74bedad9e916" />
Fig 1.2: Scaled waveform sending 0x84 Over MOSI Line
Issue to fix: The CS line is going high before a single clock cycle of data is sent, make CS stay low until all eight bits are sent over

<img width="1248" height="634" alt="Screenshot 2026-07-07 at 3 37 13 AM" src="https://github.com/user-attachments/assets/136f1f80-bf3a-4b92-b191-7957a1523f52" />
Fig 1.3: Complete validation of loopback test to ensure MISO line works appropriately, and we can echo bytes back

<img width="1246" height="632" alt="Screenshot 2026-07-07 at 2 51 12 PM" src="https://github.com/user-attachments/assets/b2e70168-0dd6-426b-a762-6d1c9785ee67" />
Fig 1.4: Set polling wall to check that RX FIFO receives byte and separate one to ensure Shift Register BSY flag set to zero, so it is done with transmission + cleanup, then raise CS high only AFTER all bits have been transceived
Final Function execution time to send and receive one byte: 41 μs, with each clock cycle taking four μs so the total transmission time was 32 μs
Time to Send Several Bytes: N/A; due to polling, we would have to guess a short time to loop before calling the function again; inaccurate.




