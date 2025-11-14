#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"     // <-- AÑADIDO (para el Mutex)

/* Nuestros módulos de configuración y tareas */
#include "config.h"
#include "tasks.h"

/* Definición del Mutex global (usado en tasks.c) */
SemaphoreHandle_t xAdcMutex;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName);

void
vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName) {
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

	/* --- 2. Creación de primitivas de FreeRTOS --- */
	
	/* Creamos el Mutex para proteger los búferes del ADC */
	xAdcMutex = xSemaphoreCreateMutex();
	if (xAdcMutex == NULL) {
		/* Error: No hay suficiente heap para crear el mutex */
		for(;;);
	}

	/* --- 3. Creación de Tareas de FreeRTOS --- */
	
	/* Tarea del LED (prioridad baja) */
	xTaskCreate(vTaskLed,
		    "LED",
		    100,
		    NULL,
		    configMAX_PRIORITIES - 2, // Prioridad 3
		    NULL);

	/* Tarea de lectura del ADC (prioridad alta) */
	xTaskCreate(vTaskReadAnalog,
		    "ADC",
		    128, // Un poco más de stack para los búferes
		    NULL,
		    configMAX_PRIORITIES - 1, // Prioridad 4 (más alta)
		    NULL);

	/* --- 4. Iniciar el Sistema --- */
	vTaskStartScheduler();

	/* Nunca debería llegar aquí */
	for (;;);
	return 0;
}
// End