#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"     // <-- AÑADIDO (para Mutex y secciones críticas)

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>

#include "app_tasks.h"

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


/**
 * @brief Tarea de control principal (PID y Salida PWM).
 */
void
vTaskControlPWM(void *pvParameters)
{
	(void)pvParameters;

	/* * Constantes para la conversión.
	 * (Setpoint Voltaje = 2.0V)
	 * (Setpoint Freq = 10000 Hz)
	 * (Setpoint Amplitud Vpp = 2.0V) -> Duty Cycle = 2.0/3.3 = 60.6%
	 */
	const float SETPOINT_VOLTS = 2.0f;
	const float MAX_VOLTS = 3.3f;
	const float TARGET_FREQ_HZ = 10000.0f;
	const float TARGET_DUTY_PCT = 2.0f / 3.3f; // 60.6%

	/* * El reloj del TIM1 es 72MHz.
	 * Periodo (ARR) = (72,000,000 / Frecuencia) - 1
	 * CCR = (ARR + 1) * Duty_Cycle_Percent
	 */
	const uint32_t TIM_CLOCK_HZ = 72000000;
	
	/* Ejecuta esta tarea de control a 50Hz (cada 20ms) */
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const TickType_t xFrequency = pdMS_TO_TICKS(20);

	for (;;)
	{
		/* 1. Espera para el próximo ciclo de control */
		vTaskDelayUntil(&xLastWakeTime, xFrequency);

		/* 2. Lee los valores de los "potenciómetros" (de forma segura) */
		float fAmplitudVolts = adc_get_amplitud_volts();
		float fFrecuenciaVolts = adc_get_frecuencia_volts();

		/* * 3. Lógica de Mapeo (Requisitos 2 y 3)
		 * "cuando en el pin de entrada hay 2Vdc"
		 * Asumiremos una relación lineal simple por ahora (sin PID).
		 * * Mapeo de Frecuencia:
		 * Si 2.0V -> 10,000 Hz
		 * f(V) = (V / 2.0V) * 10,000 Hz
		 */
		float nueva_frec_hz = (fFrecuenciaVolts / SETPOINT_VOLTS) * TARGET_FREQ_HZ;
		
		/* Asegura que la frecuencia no sea cero (evita división por cero) */
		if (nueva_frec_hz < 100.0f) { // Límite inferior de 100Hz
			nueva_frec_hz = 100.0f;
		}

		/* * Mapeo de Amplitud (Duty Cycle):
		 * Si 2.0V -> 60.6% Duty
		 * d(V) = (V / 2.0V) * 60.6%
		 */
		float nuevo_duty_pct = (fAmplitudVolts / SETPOINT_VOLTS) * TARGET_DUTY_PCT;

		/* Limita el duty cycle entre 0% y 100% */
		if (nuevo_duty_pct > 1.0f) nuevo_duty_pct = 1.0f;
		if (nuevo_duty_pct < 0.0f) nuevo_duty_pct = 0.0f;


		/* * 4. Aplicar nuevos valores al Timer (Hardware)
		 * NOTA: Esto NO está protegido por un mutex (como pide el Req. 7).
		 * Lo añadiremos después.
		 */
		
		/* Actualiza Frecuencia (Período ARR) */
		uint32_t nuevo_periodo_arr = (TIM_CLOCK_HZ / nueva_frec_hz) - 1;
		timer_set_period(TIM1, nuevo_periodo_arr);

		/* Actualiza Amplitud (Duty Cycle CCR) */
		/* (nuevo_periodo_arr + 1) == (TIM_CLOCK_HZ / nueva_frec_hz) */
		uint32_t nuevo_ccr = (uint32_t)((float)(nuevo_periodo_arr + 1) * nuevo_duty_pct);
		timer_set_oc_value(TIM1, TIM_OC1, nuevo_ccr);
	}
}