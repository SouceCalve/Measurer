#include "stm32f4xx.h"
#include "main.h"
#include <stdio.h>

/*typedef union{
	struct __attribute__((__packed__)){
		uint32_t : 12;
		uint32_t RANGE_SEL : 4;
	} bits;
	uint32_t mask;
} ADS8691_RANGE;*/

/*typedef union{
	struct __attribute__((__packed__)){
		uint32_t : 4;
		uint32_t vol_result : 18;
	} bits;
	uint32_t mask;
} conversion_voltage;

typedef union{
	struct __attribute__((__packed__)){
		uint32_t : 4;
		uint32_t cur_result : 18;
	} bits;
	uint32_t mask;
} conversion_current;*/

uint32_t conversion_voltage,conversion_current = {0};

char uart_buffer[16] = {0}; //character buffer for sending packet to pc

//Clock definition
// HSE_FREQ is 25 Mhz
#define PLLM 24
#define PLLN 192
#define PLLQ 4
#define PLLP 0

//UART defines
#define USART1_mantisa 54 //i dont give a fuck about dinamic calculation for it fucked divider - it set for 115200 baud (100Mhz), so recalculate if you need other baudrate
#define USART1_fraction 4

uint8_t range = 0; //ADC range

static void clk_init(void){
	SET_BIT(RCC->CR,RCC_CR_HSEON_Msk); //enabling HSE
	while(!READ_BIT(RCC->CR,RCC_CR_HSERDY)); //waiting for HSE to be ready
	MODIFY_REG(RCC->PLLCFGR, RCC_PLLCFGR_PLLSRC_HSE_Msk | RCC_PLLCFGR_PLLM_Msk | RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLQ_Msk | RCC_PLLCFGR_PLLP_Msk, 1 << RCC_PLLCFGR_PLLSRC_HSE_Pos | PLLM << RCC_PLLCFGR_PLLM_Pos | PLLN << RCC_PLLCFGR_PLLN_Pos | PLLQ << RCC_PLLCFGR_PLLQ_Pos | PLLP << RCC_PLLCFGR_PLLP_Pos);
	//Writing PLL dividers and multipliers
	SET_BIT(RCC->APB1ENR,RCC_APB1ENR_PWREN_Msk); //Enabling clock for power module
	SET_BIT(PWR->CR,PWR_CR_VOS_Msk); //Selecting 1.2 Volts for core in power module
	MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY_Msk, FLASH_ACR_LATENCY_3WS); //Setting up delay to 3 cycles because flash cant keep up to 100 MHz
	SET_BIT(RCC->CR,RCC_CR_PLLON_Msk); //enabling PLL
	while(!READ_BIT(RCC->CR,RCC_CR_PLLRDY)); //Waiting until PLL is ready
	SET_BIT(RCC->CFGR,RCC_CFGR_PPRE1_DIV2);//APB2 pre-divider = 2; APB2 clk = 100 / 2 = 50MHz
	SET_BIT(RCC->CFGR,RCC_CFGR_SW_PLL); //Swithing to PLL
	while(READ_BIT(RCC->CFGR,RCC_CFGR_SWS_Msk)!=RCC_CFGR_SWS_PLL);//Waiting until switching to PLL as clk source
	SystemCoreClockUpdate();
	SET_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM3EN_Msk); //enable clock for TIM3
	SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN_Msk | RCC_AHB1ENR_GPIOBEN_Msk); //enable clock for GPIOA and GPIOB
	SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_DMA2EN_Msk); //enable clock for DMA2
	SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN_Msk | RCC_APB2ENR_SPI1EN_Msk); //enable clock for USART1 and SPI1
}

static void pins_init(void){
	MODIFY_REG(GPIOA->MODER, GPIO_MODER_MODER5_Msk | GPIO_MODER_MODER6_Msk | GPIO_MODER_MODER7_Msk | GPIO_MODER_MODER9_Msk | GPIO_MODER_MODER10_Msk, 2 << GPIO_MODER_MODER5_Pos | 2 << GPIO_MODER_MODER6_Pos | 2 << GPIO_MODER_MODER7_Pos | 2 << GPIO_MODER_MODER9_Pos | 2 << GPIO_MODER_MODER10_Pos);
	//set up PA5-7,9,11-12 as alternate functions
	MODIFY_REG(GPIOB->MODER, GPIO_MODER_MODER0_Msk | GPIO_MODER_MODER1_Msk, 1 << GPIO_MODER_MODER0_Pos | 1 << GPIO_MODER_MODER1_Pos);
	//set up pb0-1 as general output
	MODIFY_REG(GPIOA->AFR[0], GPIO_AFRL_AFSEL5_Msk | GPIO_AFRL_AFSEL6_Msk | GPIO_AFRL_AFSEL7_Msk, 5 << GPIO_AFRL_AFSEL5_Pos | 5 << GPIO_AFRL_AFSEL6_Pos | 5 << GPIO_AFRL_AFSEL7_Pos);
	//setting PA5-7 to AF05
	MODIFY_REG(GPIOA->AFR[1], GPIO_AFRH_AFSEL9_Msk | GPIO_AFRH_AFSEL10_Msk, 7 << GPIO_AFRH_AFSEL9_Pos | 7 << GPIO_AFRH_AFSEL10_Pos);
	//setting PA11,12 to AF07

}

static void timer_init(void){
	SET_BIT(TIM3->DIER, TIM_DIER_UIE_Msk); //Enabling update interrupt generation
	WRITE_REG(TIM3->ARR, 10000 << TIM_ARR_ARR_Pos); //Loading upper limit in counter
	SET_BIT(TIM3->CR1,TIM_CR1_CEN_Msk); //Start the timer
	NVIC_EnableIRQ(TIM3_IRQn); //Adding TIM3 interrupt to interrupt board
	NVIC_SetPriority(TIM3_IRQn, 5); //Set priority for that interrupt to 1(behind 0)
}

static void uart_init(void){
//Fpclk = Fck = APB2
	WRITE_REG(USART1->BRR, (USART1_mantisa << 4) | USART1_fraction); //fuck it - i calculated it by hand - 115200 baud
	SET_BIT(USART1->CR1, USART_CR1_TE_Msk | USART_CR1_RE_Msk); //enabling TX and RX
	SET_BIT(USART1->CR1, USART_CR1_UE_Msk); //starting USART
}

static void spi_init(void){
	MODIFY_REG(SPI1->CR1,SPI_CR1_BR_Msk, 4 << SPI_CR1_BR_Pos); // baud rate = 100 / 32 = 3.125 Mhz
	SET_BIT(SPI1->CR1, SPI_CR1_DFF_Msk); //Selected frame size as 16 bit
	SET_BIT(SPI1->CR1, SPI_CR1_MSTR_Msk); //Selected master
	SET_BIT(SPI1->CR1, SPI_CR1_SSM_Msk); //Selected software SS managment(or else disabling hardware managment for NSS pin, what isnt configured by design)
	SET_BIT(SPI1->CR1, SPI_CR1_SSI_Msk); //Set virtual NSS pin state to high to sneak around 1 of MODEFAULT checks
	SET_BIT(SPI1->CR1, SPI_CR1_SPE_Msk); //Enabling spi
}

static uint32_t SPI_read_write_word(uint32_t data){ //Self-explanotary - just for commands
	uint32_t receive_buffer = 0; //creating buffer for received data
	while(!(READ_BIT(SPI1->SR,SPI_SR_TXE))); //Waiting until transmit buffer is ready
	WRITE_REG(SPI1->DR,data >> 16); //Writing MSB
	while(!(READ_BIT(SPI1->SR,SPI_SR_RXNE))); //Waiting until we have received data
	receive_buffer = READ_REG(SPI1->DR) << 16; //reading to sneak around another MODEFAULT check - OVERRUN
	while(!(READ_BIT(SPI1->SR,SPI_SR_TXE))); //Waiting until transmit buffer is ready
	WRITE_REG(SPI1->DR,data & 0xFFFF); //Writing LSB
	while(!(READ_BIT(SPI1->SR,SPI_SR_RXNE))); //Waiting until we have received data
	receive_buffer |= (uint32_t)READ_REG(SPI1->DR); //reading to sneak around another MODEFAULT check - OVERRUN
	while(READ_BIT(SPI1->SR,SPI_SR_BSY_Msk)); //Waiting for SPI to end transmission(just to be sure and don't cause any glitches on bus)
	return receive_buffer; //returning data inside buffer as received
}

#ifndef test_interrupt
__attribute__((used)) void TIM3_IRQHandler(void){ //fuck off keil, this is interrupt handler;
	uint16_t conversion_voltage,conversion_current = {0}; //create new variables for conversions result
	MODIFY_REG(GPIOB->ODR, GPIO_ODR_OD0_Msk, 0); //CS0 low
	conversion_voltage = SPI_read_write_word(0); //Reading first ADC
	MODIFY_REG(GPIOB->ODR, GPIO_ODR_OD0_Msk, 1); //CS0 high
	MODIFY_REG(GPIOB->ODR, GPIO_ODR_OD0_Msk, 0); //CS1 low
	conversion_current = SPI_read_write_word(0); //Reading second ADC
	MODIFY_REG(GPIOB->ODR, GPIO_ODR_OD0_Msk, 1); //CS1 high
	CLEAR_BIT(TIM3->SR, TIM_SR_UIF_Msk); //disabling interrupt
	sprintf(uart_buffer,"%d;%d\n",conversion_voltage,conversion_current); //Creating string for sending
	//SET_BIT(DMA2_Stream1->CR,DMA_SxCR_EN_Msk); //Starting one DMA transfer
	/*for (uint8_t x = 0; x<15; x++){
		while(!(READ_BIT(USART1->SR,USART_SR_TXE)));
		WRITE_REG(USART1->DR,uart_buffer[x]);
	}*/
}
#endif

static void ADC_range_change(void){
	SPI_read_write_word(0b1101000 << 25 | 0 << 24 | range_reg_addr << 16 | range); //Sending command to write word to range register with 3xVref range
}

static void DMA_init(void){
	SET_BIT(DMA2_Stream7->CR,DMA_SxCR_MINC_Msk); //Enabling memory address increment
	MODIFY_REG(DMA2_Stream7->CR, DMA_SxCR_CHSEL_Msk, 4 << DMA_SxCR_CHSEL_Pos); //Selecting Stream7 and Channel 4
	MODIFY_REG(DMA2_Stream7->NDTR, DMA_SxNDT_Msk, 16 << DMA_SxNDT_Pos); //Selecting buffer size = 16
	WRITE_REG(DMA2_Stream7->PAR,(uint32_t)(USART1_BASE+0x04)); //Setting peripheral address(where to send data)
	WRITE_REG(DMA2_Stream7->M0AR,(uint32_t)uart_buffer); //Setting up memory address for reading(from there bytes will be sent)
	MODIFY_REG(DMA2_Stream7->CR, DMA_SxCR_DIR_Msk, 1 << DMA_SxCR_DIR_Pos); //Selecting Memory to Peripheral sending
}

int main(void){
	clk_init();
	pins_init();
	timer_init();
	uart_init();
	spi_init();
	DMA_init();
#ifdef test_uart
	usart1_echo(); //if you want to test uart, just uncomment define in main.h
#endif
#ifdef test_spi
	spi_echo(); //if you want to test spi, just uncomment define in main.h
#endif
	while(1);
}
