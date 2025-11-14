#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"     // <-- AÑADIDO (para el Mutex)

/* Nuestros módulos de configuración y tareas */
#include "config.h"
#include "app_tasks.h"

/* Definición del Mutex global (usado en tasks.c) */
SemaphoreHandle_t xAdcMutex;

extern void vApplicationStackOverflowHook(xTaskHandle pxTask,signed portCHAR *pcTaskName);

void
vApplicationStackOverflowHook(xTaskHandle pxTask,signed portCHAR *pcTaskName) {
	(void)pxTask;
	(void)pcTaskName;
	for(;;);
}

/**
 * @brief Punto de entrada principal del programa.
 */
int
main(void) {

	/* --- 1. Configuración del Hardware --- */
	clock_setup();  // Configura 72MHz
	gpio_setup();   // Configura PC13 (LED) y PA0/PA1 (ADC)
	adc_dma_init(); // Configura ADC1 y DMA1 para lectura continua
	pwm_setup();    // <-- AÑADE ESTA LÍNEA (Configura TIM1 PWM en PA8)

	/* --- 2. Creación de primitivas de FreeRTOS --- */
	
	/* ... (tu código de xSemaphoreCreateMutex) ... */

	/* --- 3. Creación de Tareas de FreeRTOS --- */
	
	/* Tarea del LED (prioridad baja) */
	xTaskCreate(vTaskLed,
		    "LED",
		    100,
		    NULL,
		    configMAX_PRIORITIES - 3, // Prioridad 2 (Bajó 1)
		    NULL);

	/* Tarea de lectura del ADC (prioridad alta) */
	xTaskCreate(vTaskReadAnalog,
		    "ADC",
		    128,
		    NULL,
		    configMAX_PRIORITIES - 1, // Prioridad 4 (más alta)
		    NULL);

	/* Tarea de Control PWM (prioridad media-alta) */
	xTaskCreate(vTaskControlPWM,      // <-- AÑADE ESTE BLOQUE
		    "PWM_Ctrl",
		    128,
		    NULL,
		    configMAX_PRIORITIES - 2, // Prioridad 3 (Menos que ADC, más que LED)
		    NULL);

	/* --- 4. Iniciar el Sistema --- */
	vTaskStartScheduler();

	/* Nunca debería llegar aquí */
	for (;;);
	return 0;
}