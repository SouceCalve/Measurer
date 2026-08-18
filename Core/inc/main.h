#include <sys/stat.h>

// TESTING DEFINES
//#define test_uart
//#define test_interrupt
//#define test_spi

//Gaming with includes brings nothing else as pain///

int _close(int file) { return -1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }
int _write(int file, char *ptr, int len) { return len; }

#define range_reg_addr 0b00010100
#define treshhold_reg_addr 0b00000010

//Testing functions definition

#ifdef test_interrupt
__attribute__((used)) void TIM3_IRQHandler(void){ //fuck off keil, this is interrupt handler
	MODIFY_REG(GPIOB->ODR, GPIO_ODR_OD0_Msk, ~(READ_BIT(GPIOB->ODR,GPIO_ODR_OD0_Msk))); //Changing state for PB0 for measuring how frequent interrupt interrupts(pun intended)
	CLEAR_BIT(TIM3->SR, TIM_SR_UIF_Msk); //disabling interrupt
}
#endif


#ifdef test_spi
static void spi_echo(void){ //echo function to test spi
	uint16_t payload = 0x4141;
	while(1){
		if(READ_BIT(SPI1->SR, SPI_SR_RXNE)){
			while(!(READ_BIT(USART1->SR,USART_SR_TXE)));
			WRITE_REG(USART1->DR,READ_REG(SPI1->DR));
		}
		else{
			while(!(READ_BIT(SPI1->SR,SPI_SR_TXE)));
			WRITE_REG(SPI1->DR,payload);
		}
	}
}
#endif


#ifdef test_uart
uint32_t count = 0;

static void usart1_echo(void){ //echo function to test uart
	while(1){
		if(READ_BIT(USART1->SR, USART_SR_RXNE)){
			while(!(READ_BIT(USART1->SR,USART_SR_TXE)));
			WRITE_REG(USART1->DR,READ_REG(USART1->DR));
		}
		else{
			while(!(READ_BIT(USART1->SR,USART_SR_TXE)));
			WRITE_REG(USART1->DR,count);
			count=0;
		}
	}
}
#endif
