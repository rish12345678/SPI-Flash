# STM32 SPI Driver

Register-level SPI driver for the STM32L476RG implemented entirely without HAL.

## Project Goal

Rather than relying on STM32 HAL, this project develops a SPI peripheral driver directly from the reference manual.

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

<img width="1247" height="634" alt="Screenshot 2026-07-18 at 4 13 53 PM" src="https://github.com/user-attachments/assets/fb9b6e1a-1541-4bd9-b95a-4993572af420" />

#### Latency Cause: BL + Function Epilogue + Setting incoming\_idx and outgoing\_idx to zero

#### Dissassembly:

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
Pushing the single register r7 onto the stack takes a flat 2 clock cycles.
The add zero is done purely in registers taking 1 clock cycle.

Total: 10 clock cycles
Now, setting the two variables to zero takes a load (ldr) which is PC relative, so that is 2 clock cycles.  Although we are doing a 
read from Flash to grab the variable's address, that value was likely already cached, leading to relatively fast and reliable speeds.
Moving a literal into a register is just 1 clock cycle.
Lastly, storing a byte is 2 clock cycles.


This comes out to about 15 clock cycles.  Taking a look at this it is clear that the biggest latency bottleneck here 
by a long shot is the variable setting.  Reducing those two variable write times as well as removing function call overhead 
could significantly reduce function call to CS-Low latency.


## Improving Function Call To CS-Low Latency

Taking into consideration the latency bottlenecks we found above, I made some design decisions and subsequent changes to reduce latency.
Firstly, in order to remove the function prologue latency, I wanted to avoid the branch and link instruction and have the compiler place 
the SPI_SEND_BYTE_POLLING() function's instruction addresses sequentially in memory following the preceding code.
I did this simple by making the function inline so that it gets compiled how I intended above.



Secondly, there was the largest bottleneck of taking roughly 10 clock cycles to set two uint8_t's at the beginning of each SPI_SEND_BYTE_POLLING() 
function call.  To address this I took a close look at the properties of those two variables, such as when they are read, when they are written too, by 
which "thread" of execution.  It became clear that my previous design decisions had been purely code organization focused, and not performance-focused.
Thus, I moved the declaration and definition of the variables directly into the function itself, and removed its volatile property, because I realized two things:

1. These variables are not used by any function other than SPI_SEND_BYTE_POLLING(), so they do not need to be global variables as they were before.

2. These variables can not be accessed by any other thread of execution, such as an ISR.  Thus, they do not need to be pulled from RAM for every read or write.
   Since the variables sit solely in the function and never get passed out, they don't even need to be at a memory address, allowing the compiler to assign them
   to only a register for the duration of the function.

#### After these changes, here is the new disassembly:


      23        		Toggle_Profile_Pin_High();
      0800020e:   mov.w   r3, #1207959552 @ 0x48000000
      08000212:   movs    r2, #2
      08000214:   str     r2, [r3, #24]
      25        	uint8_t incoming_idx = 0;
      08000216:   movs    r3, #0
      08000218:   strb    r3, [r7, #7]
      26        	uint8_t outgoing_idx = 0;
      0800021a:   movs    r3, #0
      0800021c:   strb    r3, [r7, #6]

Based on the disassembly, it is clear that our five clock-cycle zero-assignment operations are now only three clock cycles.  One clock cycle for the move instructions, and 
two for the store instructions, totalling six clock cycles for the two assignments.

We also see that in this continuous snippet of instructions, there is absolutely zero function call overhead now due to inlining.  The lack of the BL instruction + the function 
prologue saved us another four clock cycles.

## In total, we eliminated eight clock cycles!




### Improved Function Call To CS-Low Latency Waveform

<img width="1249" height="639" alt="Screenshot 2026-07-18 at 5 08 12 PM" src="https://github.com/user-attachments/assets/5736b505-fa00-41bb-9678-4ab00dfa340f" />

We reduced latency from 5.5 μs to only 2.5 μs, cutting down the Function Call to CS-Low Latency by 54%.

<img width="1249" height="628" alt="Screenshot 2026-07-19 at 2 42 48 AM" src="https://github.com/user-attachments/assets/d9f97586-e3a4-413c-8998-206ef3de6edd" />

In the image above, we can see we were also able to cut down the CS-High to main() return latency from 2.5 μs to only 1 μs, rendering a 60% reduction in latency from CS-High to the following instruction in main().
This is definitely attributed to the removal of the function call through inlining, which conveniently removed the function epilogue as well.  We can see that in this disassembly:

      46        	GPIOA->ODR |= (1U << 4);
      08000284:   mov.w   r3, #1207959552 @ 0x48000000
      08000288:   ldr     r3, [r3, #20]
      0800028a:   mov.w   r2, #1207959552 @ 0x48000000
      0800028e:   orr.w   r3, r3, #16
      08000292:   str     r3, [r2, #20]
      47        }
      08000294:   nop     
      25        		Toggle_Profile_Pin_Low();
      08000296:   mov.w   r3, #1207959552 @ 0x48000000
      0800029a:   movs    r2, #2
      0800029c:   str     r2, [r3, #40]   @ 0x28

Clearly, there is only the no-op instruction that was placed in between CS-High and Toggle_Profile_Pin_Low, with no function epilogue instructions from before.

## Updated Latency Values

1. Function Call to CS Line Low latency: 2.50 μs (54% latency reduction from initial build)
2. CS Line Low to First SPI Clock Edge Latency: 11.00 - 11.50 μs
3. End of Last Clock Cycle to CS Line High Latency: 0.00 μs
4. CS Line High to Return to Main Latency: 1.00 μs (60% latency reduction from initial build)
 

### Polling Waveform Progression

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



### Interrupts Waveform Progression



<img width="1246" height="636" alt="Screenshot 2026-07-22 at 6 06 18 PM" src="https://github.com/user-attachments/assets/11102111-899d-49b8-a976-4ff72f98fc0b" />

Fig 1.5:  This is the initial waveform of SPI sending over a single array of four uint8_t's. Almost every pin has something wrong with it.  I will continue documenting what I figure out through debugging this non-blocking version.

<img width="1246" height="757" alt="Screenshot 2026-07-22 at 10 48 35 PM" src="https://github.com/user-attachments/assets/738dfc93-f1f0-499d-bc15-39ab5e898102" />

Fig 1.6:  After stepping through the code and looking at SPI peripheral register states, I noticed that immediately after gating clock to SPI, RXNE was set to 1, meaning there was some previous garbage value in there, making us enter the RXNE conditional(the first one) on the first interrupt, adding that to the incoming array and incrementing the pointer, before even adding the first value to the TX buffer that we want to send.  By first cleaning out the RX buffer of any extreneuous values, we got the CS line to match up with our transmission, going high only after all bytes have been sent.



<img width="1248" height="630" alt="Screenshot 2026-07-23 at 1 47 17 PM" src="https://github.com/user-attachments/assets/a38b4bc6-68ea-4b7d-9aec-080406e41f00" />

Fig 1.7:  There is still a small dip and then reset right before CS goes low; this happens only in the GPIO_init() helper function, where the pins are undergoing different changes, such as MODER changes and AFR changes.  Since this happens once at setup, pre-transmission (CS is still high), we can effectively ignore it, keeping the transmission untampered with and not adding to latency.  The actual transmission is coming across the lines perfectly.  The only thing I can think to make the transmission better, which it appears to only affect the first transmission is that the CS line goes low long before the transmission actually starts, and even clock and MOSI lines are set to their ready state to send the first bit.  Since it is a falling clock edge, meaning it is pushing a voltage or not onto the line and not sampling the line, it shouldn't affect the transmission, but it does end up making the CS Line Low to First SPI Clock Edge Latency 87.75 μs, compared to the 11.00 - 11.50 μs we got for the same interval in the blocking version.




<img width="1251" height="632" alt="Screenshot 2026-07-23 at 6 56 33 PM" src="https://github.com/user-attachments/assets/8b88665b-c149-46fc-93e3-bf9aacdec58b" />
<img width="1249" height="629" alt="Screenshot 2026-07-23 at 6 57 28 PM" src="https://github.com/user-attachments/assets/f02a07a8-eabf-4afb-8152-b0f4b433fda2" />




Fig 1.8 & Fig 1.9: Just for a sanity check I made the loopback test such that if any bytes that was sent out on the MOSI line was different from what we got back on the MISO line, we raise the profile pin up and drop it after an interval following the transmission.  I made sure this is working with a simple transmission kick-off + changing too values in the incoming bytes array as such:

      incoming_arr[0] = 0x00;
      incoming_arr[3] = 0xFF;


Then I ran the profiler validation code to check for the incorrect bytes in a loop.  In the first picture we see two jumps on the profile pin indicating two incorrect bytes in the incoming_arr, and then in the second picture I got rid of the two lines of code above, and it can back to the second picture where there are no jumps of the profiler line.
