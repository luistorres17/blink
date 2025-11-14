#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"     // <-- AÑADIDO (para Mutex y secciones críticas)

#include <libopencm3/stm32/gpio.h>

#include "tasks.h"

/* ========= Búferes y Estado del Módulo ADC ========= */

/*
 * Búfer de destino del DMA.
 * Es global (no estático) para que config.c pueda verlo
 * usando 'extern'. El hardware DMA escribe aquí.
 */
volatile uint16_t adc_dma_buffer[2]; // [0]=amp, [1]=freq

/* Búferes de promedio (internos a este archivo) */
static uint16_t amp_buffer[MUESTRAS_PID];
static uint16_t freq_buffer[MUESTRAS_PID];
static unsigned idx_pid = 0;

/* Mutex (definido en main.c) para proteger los búferes de promedio */
extern SemaphoreHandle_t xAdcMutex;


/* ========= Helpers Internos (copiados de inputs_adc.c) ========= */

static inline float __u12_to_volts(uint16_t raw)
{
	/* ADC de 12 bits: 0..4095 */
	return (VREF_VOLTS * (float)raw) / 4095.0f;
}

static uint16_t __avg_u16(const uint16_t *buf, unsigned n)
{
	uint32_t acc = 0;
	for (unsigned i = 0; i < n; ++i) acc += buf[i];
	return (uint16_t)(acc / n);
}


/* ========= Tareas de la Aplicación ========= */

/**
 * @brief Tarea de parpadeo del LED (Corregida con vTaskDelay).
 */
void
vTaskLed(void *pvParameters) {
	(void)pvParameters;

	for (;;) {
		/* Conmuta el estado del LED */
		gpio_toggle(GPIOC,GPIO13);
		
		/*
		 * Demora RTOS-friendly.
		 * Duerme la tarea por 500ms (ticks) y permite
		 * que otras tareas (como vTaskReadAnalog) se ejecuten.
		 */
		vTaskDelay(pdMS_TO_TICKS(500)); // <-- CORREGIDO
	}
}

/**
 * @brief Tarea de lectura periódica del ADC (Lógica de iniciar_entradas_adc).
 */
void
vTaskReadAnalog(void *pvParameters)
{
	(void)pvParameters;

	/* Inicializa vTaskDelayUntil para una ejecución periódica precisa */
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms -> 100Hz

	for (;;)
	{
		/* 1. Espera hasta que sea el momento de la próxima ejecución */
		vTaskDelayUntil(&xLastWakeTime, xFrequency);

		/* 2. Copia atómica del búfer de DMA (protegido contra cambios de tarea) */
		uint16_t amp, freq;
		taskENTER_CRITICAL(); // Reemplaza cm_disable_interrupts()
		{
			amp  = adc_dma_buffer[0];
			freq = adc_dma_buffer[1];
		}
		taskEXIT_CRITICAL(); // Reemplaza cm_enable_interrupts()

		/* 3. Actualiza los búferes de promedio (protegido por Mutex) */
		if (xAdcMutex != NULL)
		{
			/* Espera hasta 10 ticks (ms) para obtener el mutex */
			if (xSemaphoreTake(xAdcMutex, (TickType_t)10) == pdTRUE)
			{
				/* Sección crítica (acceso a datos compartidos) */
				amp_buffer[idx_pid]  = amp;
				freq_buffer[idx_pid] = freq;
				idx_pid = (idx_pid + 1) % MUESTRAS_PID;
				
				xSemaphoreGive(xAdcMutex);
			}
			/* Si no se obtiene el mutex, simplemente omitimos esta muestra.
			 * Esto evita bloquear la tarea del ADC. */
		}
	}
}


/* ========= API de Getters (Implementación) ========= */

float adc_get_amplitud_volts(void)
{
	uint16_t avg_raw = 0;
	if (xAdcMutex != NULL)
	{
		if (xSemaphoreTake(xAdcMutex, (TickType_t)10) == pdTRUE)
		{
			/* Lee el búfer de promedio de forma segura */
			avg_raw = __avg_u16(amp_buffer, MUESTRAS_PID);
			xSemaphoreGive(xAdcMutex);
		}
	}
	return __u12_to_volts(avg_raw);
}

float adc_get_frecuencia_volts(void)
{
	uint16_t avg_raw = 0;
	if (xAdcMutex != NULL)
	{
		if (xSemaphoreTake(xAdcMutex, (TickType_t)10) == pdTRUE)
		{
			/* Lee el búfer de promedio de forma segura */
			avg_raw = __avg_u16(freq_buffer, MUESTRAS_PID);
			xSemaphoreGive(xAdcMutex);
		}
	}
	return __u12_to_volts(avg_raw);
}