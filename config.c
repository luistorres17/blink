#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>   // <-- AÑADIDO
#include <libopencm3/stm32/dma.h>   // <-- AÑADIDO
#include <libopencm3/stm32/timer.h> // Para timer_reset() y otrascle
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


/**
 * @brief Configura el TIM1 en modo PWM en el pin PA8.
 */
void pwm_setup(void)
{
	/* 1. Habilitar relojes para TIM1 y GPIOA (AFIO ya debe estar por GPIO) */
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_AFIO); // Necesario para funciones alternas

	/* 2. Configurar PA8 como Salida de Función Alterna (Push-Pull) */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);

	/* 3. Configuración básica del Timer (TIM1) */
	timer_reset(TIM1);
	timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, // Divisor de reloj
		       TIM_CR1_CMS_EDGE,         // Alineado al borde
		       TIM_CR1_DIR_UP);          // Conteo ascendente

	/* * 4. Configuración de Frecuencia (Período)
	 * Frecuencia deseada = 10 KHz (Requisito 3)
	 * Reloj del Timer (APB2) = 72 MHz (por clock_setup)
	 * Prescaler = 0 (divide por 1)
	 *
	 * Periodo (ARR) = (Reloj_Timer / Frecuencia) - 1
	 * Periodo (ARR) = (72,000,000 / 10,000) - 1 = 7200 - 1 = 7199
	 */
	timer_set_prescaler(TIM1, 0); 
	timer_set_period(TIM1, 7199);

	/* 5. Configuración del Canal PWM (TIM1_CH1 en PA8) */
	timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_PWM1); // Modo PWM 1
	timer_enable_oc_output(TIM1, TIM_OC1);        // Habilitar salida del canal 1
	
	/* * 6. Configuración del Ciclo de Trabajo (Amplitud)
	 * Amplitud deseada = 2Vpp (Requisito 2)
	 * Esto se traduce a un ciclo de trabajo. Asumamos 50% (2.5Vpp en una señal 
     * de 0-5V, o 1.65Vpp en una de 0-3.3V). Para 2Vpp en 3.3V, necesitamos
     * un duty cycle de (2.0 / 3.3) * 100 = ~60.6%
	 * * CCR = (ARR + 1) * (DutyCycle / 100)
	 * CCR = 7200 * (60.6 / 100) = 4363
	 * * Empecemos con 50% (Amplitud = 3.3Vpp / 2 = 1.65Vpp)
	 * CCR = 7200 * 0.50 = 3600
	 */
	timer_set_oc_value(TIM1, TIM_OC1, 3600); // 50% duty cycle inicial

	/* 7. Habilitar el Timer */
	timer_enable_break_main_output(TIM1); // Necesario para TIM1
	timer_enable_counter(TIM1);
}