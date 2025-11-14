#ifndef CONFIG_H
#define CONFIG_H

// Añade esta línea para incluir los timers
#include <libopencm3/stm32/timer.h> 

/**
 * @brief Configura el reloj principal del sistema (SYSCLK a 72MHz).
 */
//... (el resto de tus funciones)

/**
 * @brief Configura el hardware ADC1 y DMA1 (Canal 1) en modo circular.
 */
void adc_dma_init(void);

/**
 * @brief Configura el TIM1 en modo PWM en el pin PA8.
 */
void pwm_setup(void); // <-- AÑADE ESTA LÍNEA

#endif // CONFIG_H