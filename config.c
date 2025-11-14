#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>   // <-- AÑADIDO
#include <libopencm3/stm32/dma.h>   // <-- AÑADIDO
#include "config.h"

/*
 * Búfer de destino para el DMA.
 * El DMA (hardware) escribirá aquí. La tarea (software) leerá desde aquí.
 * 'volatile' es crucial. 'extern' significa que está definido en otro
 * archivo (en nuestro caso, tasks.c).
 */
extern volatile uint16_t adc_dma_buffer[2];


/**
 * @brief Configura el reloj principal del sistema (SYSCLK a 72MHz).
 */
void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
}

/**
 * @brief Configura los pines GPIO necesarios para la aplicación.
 */
void gpio_setup(void)
{
	/* 1. Habilitar relojes de puertos GPIO */
	rcc_periph_clock_enable(RCC_GPIOC); // Para el LED
	rcc_periph_clock_enable(RCC_GPIOA); // Para el ADC (PA0, PA1)

	/* 2. Configurar el pin del LED (PC13) como salida */
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
	
	/* 3. Configurar pines del ADC (PA0, PA1) como entrada analógica */
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
		      GPIO_CNF_INPUT_ANALOG, GPIO0 | GPIO1);
}

/**
 * @brief Configura el hardware ADC1 y DMA1 (Canal 1) en modo circular.
 */
void adc_dma_init(void)
{
	/* 1. Habilitar relojes para ADC1 y DMA1 */
	rcc_periph_clock_enable(RCC_DMA1);
	rcc_periph_clock_enable(RCC_ADC1);

	/* 2. Configurar el DMA (Canal 1 para ADC1) */
	dma_channel_reset(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_address(DMA1, DMA_CHANNEL1, (uint32_t)&ADC_DR(ADC1));
	dma_set_memory_address(DMA1, DMA_CHANNEL1, (uint32_t)adc_dma_buffer);
	dma_set_number_of_data(DMA1, DMA_CHANNEL1, 2); // Dos canales (Amp, Freq)
	dma_set_read_from_peripheral(DMA1, DMA_CHANNEL1);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL1);
	dma_enable_circular_mode(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_size(DMA1, DMA_CHANNEL1, DMA_CCR_PSIZE_16BIT);
	dma_set_memory_size(DMA1, DMA_CHANNEL1, DMA_CCR_MSIZE_16BIT);
	dma_set_priority(DMA1, DMA_CHANNEL1, DMA_CCR_PL_HIGH);

	/* 3. Configurar el ADC1 */
	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_continuous_conversion_mode(ADC1); // Modo continuo
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);
	adc_enable_scan_mode(ADC1); // Modo Scan para múltiples canales
	
	adc_power_on(ADC1);

	/* Pequeño retardo para estabilización del ADC */
	for (volatile int i = 0; i < 80000; ++i) __asm__("nop");

	/* Calibración del ADC (importante para precisión) */
	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);

	/* Definir la secuencia de canales (PA0 -> CH0, PA1 -> CH1) */
	uint8_t channels[] = {0, 1};
	adc_set_regular_sequence(ADC1, 2, channels);

	/* 4. Conectar ADC con DMA y arrancar */
	adc_enable_dma(ADC1);
	adc_start_conversion_regular(ADC1);
}