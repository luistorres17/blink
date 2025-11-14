#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief Configura el reloj principal del sistema (SYSCLK a 72MHz).
 */
void clock_setup(void);

/**
 * @brief Configura los pines GPIO necesarios para la aplicación.
 */
void gpio_setup(void);

/**
 * @brief Configura el hardware ADC1 y DMA1 (Canal 1) en modo circular.
 *
 * Prepara el hardware para leer PA0 (Amplitud) y PA1 (Frecuencia)
 * de forma continua y automática, escribiendo los resultados en
 * un búfer (definido en tasks.c).
 */
void adc_dma_init(void); // <-- AÑADIDO

#endif // CONFIG_H