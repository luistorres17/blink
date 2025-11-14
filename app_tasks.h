#ifndef APP_TASKS_H
#define APP_TASKS_H

/* ========= Constantes de la Aplicación ========= */

/* Tamaño del promedio (PID). */
#define MUESTRAS_PID 8

/* VREF por defecto (voltios) para conversión. */
#define VREF_VOLTS 3.3f


/* ========= Tareas ========= */

/**
 * @brief Tarea de parpadeo del LED (RTOS-friendly).
 */
void vTaskLed(void *pvParameters);

/**
 * @brief Tarea de lectura periódica del ADC.
 *
 * Copia los valores del búfer de DMA a un búfer de promedios,
 * de forma segura (usando un Mutex).
 */
void vTaskReadAnalog(void *pvParameters); // <-- AÑADIDO


/* ========= API de Getters (Seguros para Tareas) ========= */

/**
 * @brief Obtiene el valor promedio de Amplitud en Volts.
 * @return Valor filtrado (0.0f a VREF_VOLTS).
 */
float adc_get_amplitud_volts(void); // <-- AÑADIDO

/**
 * @brief Obtiene el valor promedio de Frecuencia en Volts.
 * @return Valor filtrado (0.0f a VREF_VOLTS).
 */
float adc_get_frecuencia_volts(void); // <-- AÑADIDO


/**
 * @brief Tarea de control principal (PID y Salida PWM).
 *
 * Lee los valores de los ADC (usando los getters) y ajusta
 * la frecuencia (Período) y amplitud (Duty Cycle) del TIM1.
 */
void vTaskControlPWM(void *pvParameters);

#endif // APP_TASKS_H