#include "spi.h"
#include "bsp.h"


/*
 * Private Setup Functions
 */

// Turn on clock for GPIOA and SPI1
static void clock_init(void);

// Set mode for GPIOA (AF)
static void GPIO_init(void);

// Configure SPI Peripheral

// SPI_CR1 Register Configuration
static void SPI_CR1_setup(void);

// SPI_CR2 Register Configuration
static void SPI_CR2_setup(void);



// Global transmission coordination functions (ISR-Friendly)
volatile uint8_t transfer_arr[TRANSFER_LEN] = {0x24, 0x48, 0x11, 0x54};

volatile uint8_t incoming_arr[TRANSFER_LEN];

uint8_t incoming_idx = 0;
uint8_t outgoing_idx = 0;


void SPI_Setup(void) {
	NVIC_EnableIRQ(SPI1_IRQn);

	clock_init();
	GPIO_init();
	SPI_CR1_setup();
	SPI_CR2_setup();
}

void SPI_IT_Trigger(void) {
	// Reset array idx values before sending new array of bytes

	// to Drain the RX Buffer
	// while RX buffer not empty, pop of RX buffer and do dummy usage to avoid compiler warnings
	while (SPI1->SR & SPI_SR_RXNE) {
		volatile uint8_t dummy = *(__IO uint8_t *)&SPI1->DR;
		(void)dummy;
	}

	incoming_idx = 0;
	outgoing_idx = 0;

	CS_LOW();

	// Allow RXNE and TXE to send interrupt trigger to NVIC, kicking off interrupt
	SPI1->CR2 |= SPI_CR2_RXNEIE;
	SPI1->CR2 |= SPI_CR2_TXEIE;
}


/* Drop CS line to low
 *
 * While TX buffer is three or more out of four bytes full, poll
 *
 * Once TX is empty, load in 0x84, from there hardware handles transmission of byte
 *
 * While the RX buffer is empty, poll
 *
 * Once RX not empty, raise CS line to high and return whats in shift register
 */



static void clock_init(void) {
	// Set both bits to one enabling clock
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN; // Gate clock to GPIO (A)
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; // Gate clock to SPI1
}

static void GPIO_init(void) {

	Toggle_Profile_Pin_High();

	// Set GPIO Port A pins 5, 6, 7 to AF - Part of SPI bus now
	GPIOA->MODER &= ~(GPIO_MODER_MODE5 | GPIO_MODER_MODE6 | GPIO_MODER_MODE7);
	GPIOA->MODER |= (GPIO_MODER_MODE5_1 | GPIO_MODER_MODE6_1 | GPIO_MODER_MODE7_1);
	Toggle_Profile_Pin_Low();





	// Set these four pins to belong to the SPI1 Peripheral (Will hardcode chip select pin PA4)
	GPIOA->AFR[0] &= ~(0xF << GPIO_AFRL_AFSEL5_Pos | 0xF << GPIO_AFRL_AFSEL6_Pos | 0xF << GPIO_AFRL_AFSEL7_Pos); // Clear for pins 5 6 7
	GPIOA->AFR[0] |= ( (GPIO_AFRL_AFSEL5_0 | GPIO_AFRL_AFSEL5_2) | (GPIO_AFRL_AFSEL6_0 | GPIO_AFRL_AFSEL6_2) | (GPIO_AFRL_AFSEL7_0 | GPIO_AFRL_AFSEL7_2));


	Toggle_Profile_Pin_High();

	// Set Pin speed for SCK, MISO, and MOSI pins to high for sharp squarish clock edges
	GPIOA->OSPEEDR |= (GPIO_OSPEEDR_OSPEED5_1 | GPIO_OSPEEDR_OSPEED6_1 | GPIO_OSPEEDR_OSPEED7_1);
	Toggle_Profile_Pin_Low();

	// Initialize the Chip Select Pin PA4, as a traditional output, will drive high, low when needed


	GPIOA->MODER &= ~(GPIO_MODER_MODE4_Msk);
	GPIOA->MODER |= GPIO_MODER_MODE4_0;

//	GPIOA->ODR |= (1U << 4);
}

static void SPI_CR1_setup(void) {
	SPI1->CR1 = 0x0000; // Ensure reset value first

	// Divide Micro-controller clock (4 MHz) by 16 to avoid early hardware related issues
	// Set Baud Rate to 250 kHz
	SPI1->CR1 &= ~(SPI_CR1_BR_Msk);
	SPI1->CR1 |= (SPI_CR1_BR_0 | SPI_CR1_BR_1);

	// Set CPOL to zero -> Clock at 0 when idle
	// Set CPHA to zero -> Data capture occurs on first clock edge (rising edge)
	SPI1->CR1 &= ~(SPI_CR1_CPOL_Msk | SPI_CR1_CPHA_Msk); // Standard for SPI Flash



	// ----*  *~* *~* *~* *~*
	// Software Slave Management and Master Mode
	SPI1->CR1 |= (SPI_CR1_SSM | SPI_CR1_SSI);
	SPI1->CR1 |= SPI_CR1_MSTR;
}

static void SPI_CR2_setup(void) {
	// Set Data Length to eight bits
	SPI1->CR2 &= ~(SPI_CR2_DS_Msk);
	SPI1->CR2 |= (0x7U << SPI_CR2_DS_Pos);

	// RXNE flag triggered when FIFO level >= 8 bits
	SPI1->CR2 |= SPI_CR2_FRXTH;

	// Also just set enable bit here instead of another function
	SPI1->CR1 |= SPI_CR1_SPE;
}

void SPI1_IRQHandler(void) {
    // Jump into this ISR for byte transfers

	// Pseudo
	/*
	 * Interrupt fires
	 * bounce right into here
	 * if (RXNE) -> we know that at least the RX having a byte fired the interrupt, so service it(pop off, add to incoming array, increment incoming array pointer)
	 * if (TXE) -> we know that at least the TX being empty fired the interrupt, so service that(push to TX buffer + incr outgoing array pointer)
	 */

	// if RX has a byte it in (threshold set to eight bits)
	Toggle_Profile_Pin_High();
	if ((SPI1->SR & SPI_SR_RXNE) && (SPI1->CR2 & SPI_CR2_RXNEIE)) {
		// Read plus incr incoming_idx, to service and turn off ringing interrupt
		incoming_arr[incoming_idx] = *(__IO uint8_t *)&SPI1->DR;
		incoming_idx++;

		// if incoming_idx gets sent past the end of arr (last byte has been sent)
		// then turn of TXE interrupt sending, TXE can have any flag, wont deciding sending interrupt to NVIC
		if (incoming_idx >= TRANSFER_LEN) {
			SPI1->CR2 &= ~SPI_CR2_RXNEIE;
			while (SPI1->SR & SPI_SR_BSY);
			CS_HIGH();
		}
	}

	// if TXEIE not set, never enter second condition -> All bytes been sent
	// if TX is empty (the one byte no longer sits there)
	if ((SPI1->SR & SPI_SR_TXE) && (SPI1->CR2 & SPI_CR2_TXEIE)) {
		// Write to the TX buffer + incr outgoing_idx, turns off this ringing interrupt
		*(__IO uint8_t *)&SPI1->DR = transfer_arr[outgoing_idx]; // Sample Data to send 0x84 | 0b 1000 0100
		outgoing_idx++;

		// if outgoing_idx gets sent past the end of arr (last byte has been sent)
		// then turn of TXE interrupt sending, TXE can have any flag, wont deciding sending interrupt to NVIC
		if (outgoing_idx >= TRANSFER_LEN) {
			SPI1->CR2 &= ~SPI_CR2_TXEIE;
		}
	}
	Toggle_Profile_Pin_Low();
}
