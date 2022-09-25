/*
 * main() simply sets up the hardware, creates all the demo application tasks,
 * then starts the scheduler.
 *
 * In addition to a subset of the standard demo application tasks, main.c also
 * defines the following tasks:
 *
 * + A 'Print' task.  The print task is the only task permitted to access the
 * LCD - thus ensuring mutual exclusion and consistent access to the resource.
 * Other tasks do not access the LCD directly, but instead send the text they
 * wish to display to the print task.  The print task spends most of its time
 * blocked - only waking when a message is queued for display.
 */
#include <stdio.h>
#include <string.h>
#include "printf-stdarg.c"

/* Environment includes. */
#include "DriverLib.h"
/* Scheduler includes. */
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
/* Demo app includes. */
#include "integer.h"
#include "PollQ.h"
#include "semtest.h"
#include "BlockQ.h"
#include "osram96x16.h"

/* STACK SIZES */
//configMINIMAL_STACK_SIZE = 70
#define configSENSOR_STACK_SIZE     ( ( unsigned short ) (40))  	//con MINIMAL_STACK -> watermark=34, despues de reducir da 4
#define configFILTER_STACK_SIZE     ( ( unsigned short ) (70)) 		//con MINIMAL_STACK -> watermark=2
#define configGRAFIC_STACK_SIZE     ( ( unsigned short ) (90)) 		//con MINIMAL_STACK -> watermark=0, despues de ampliar da 6
#define configTOP_STACK_SIZE        ( ( unsigned short ) (120)) 	//con MINIMAL_STACK -> watermark=0, despues de ampliar da 10

/* Delay between cycles of the 'check' task. */
#define mainCHECK_DELAY ((TickType_t)5000 / portTICK_PERIOD_MS)
/* UART configuration - note this does not use the FIFO so is not very
efficient. */
#define mainBAUD_RATE (19200)
#define mainFIFO_SET (0x10)
/* Demo task priorities. */
#define mainQUEUE_POLL_PRIORITY (tskIDLE_PRIORITY + 2)
#define mainCHECK_TASK_PRIORITY (tskIDLE_PRIORITY + 3)
#define mainSEM_TEST_PRIORITY (tskIDLE_PRIORITY + 1)
#define mainBLOCK_Q_PRIORITY (tskIDLE_PRIORITY + 2)
/* Misc. */
#define mainQUEUE_SIZE (3)
#define mainDEBOUNCE_DELAY ((TickType_t)150 / portTICK_PERIOD_MS)
#define mainNO_DELAY ((TickType_t)0)

/***************************************************/
#define MAX_VALUE_N 10

/*definicion de tareas*/
static void prvSetupHardware(void);
static void vSensorTask(void *pvParameters);
static void vFilterTask(void *pvParameters);
static void vGraficTask(void *pvParameters);
static void vTopTask(void *pvParameters);

/* definicion de funciones */
char *itoa(int val);
char* longToChar(unsigned long value, char *ptr, int base);
static void uartPrint(char * msg,uint8_t len);
void vOwnTaskGetRunTimeStats(signed char *pcWriteBuffer);
static void to_terminal(char* s);

/* String that is transmitted on the UART. */
static volatile char *pcNextChar;

/* Tamaño del array de la tarea del Filtro */
unsigned int N;

/* The queue used to send strings to the print task for display on the LCD. */
QueueHandle_t xPrintQueue;
/* Envia un int para ser procesado */
QueueHandle_t xValueQueue;
/* Envia el n recibido por UART a la Task del filtro */
QueueHandle_t xFilterQueue;

/* guarda el "id" de la tarea al momento de crearla, es necesario para la implementacion del watermark*/
TaskHandle_t xHandle_Sensor = NULL;
TaskHandle_t xHandle_Filter = NULL;
TaskHandle_t xHandle_Grafic = NULL;
TaskHandle_t xHandle_Top = NULL;

//array de estructuras que contienen la informacion de cada task
TaskStatus_t pxTaskStatusArray [5];


/*--------------------------------MAIN------------------------------------------*/

int main(void)
{
	// /* Configure the clocks, UART and GPIO. */
	prvSetupHardware(); //Configuro el OSRAM

	/* Create the queue used to pass message to Task. */
	xPrintQueue = xQueueCreate(mainQUEUE_SIZE, sizeof(int));
	xValueQueue = xQueueCreate(mainQUEUE_SIZE, sizeof(int));
	xFilterQueue = xQueueCreate(mainQUEUE_SIZE, sizeof(int));

	xTaskCreate(vSensorTask, "Sensor", configSENSOR_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, &xHandle_Sensor);
	xTaskCreate(vFilterTask, "Filter", configFILTER_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, &xHandle_Filter);
	xTaskCreate(vGraficTask, "Grafic", configGRAFIC_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, &xHandle_Grafic);
	xTaskCreate(vTopTask, "Top", configTOP_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, &xHandle_Top);

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* Will only get here if there was insufficient heap to start the
	scheduler. */

	return 0;
}

//Configure the processor and peripherals for this demo.
static void prvSetupHardware(void)
{
	/* Setup the PLL. */
	SysCtlClockSet(SYSCTL_SYSDIV_10 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_6MHZ);

	/* Enable the UART.  */
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

	//Set GPIO A0 and A1 as peripheral function.  They are used to output theUART signals.
    GPIODirModeSet(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_DIR_MODE_HW);

	/* Configure the UART for 8-N-1 operation. */
	UARTConfigSet(UART0_BASE, mainBAUD_RATE, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);

	IntPrioritySet(INT_UART0, configKERNEL_INTERRUPT_PRIORITY);
	IntEnable(INT_UART0);

	UARTIntEnable( UART0_BASE, UART_INT_RX);

	/* Initialise the LCD */
	OSRAMInit(false);
	OSRAMStringDraw("www.FreeRTOS.org", 0, 0);
	OSRAMStringDraw("LM3S811 demo", 16, 1);
}

/*--------------------------------UART------------------------------------------*/

//Configuracion de la interrupcion por UART0
void vUART_ISR(void)
{
	unsigned long ulStatus;
	unsigned long retval;
	unsigned int n = 10;

	/* What caused the interrupt. */
	ulStatus = UARTIntStatus(UART0_BASE, pdTRUE);

	/* Clear the interrupt. */
	UARTIntClear(UART0_BASE, ulStatus);

	/* Was a Tx interrupt pending? */
	if (ulStatus & UART_INT_TX)
	{
		/* Send the next character in the string.  We are not using the FIFO. */
		if (*pcNextChar != 0)
		{
			if (!(HWREG(UART0_BASE + UART_O_FR) & UART_FR_TXFF))
			{
				HWREG(UART0_BASE + UART_O_DR) = *pcNextChar;
			}
			pcNextChar++;
		}
	}

	

	/* Was a Rx interrupt pending? */
	if( ulStatus & UART_INT_RX )
	{
		retval = UARTCharGet(UART0_BASE); 
		if(retval >= 48 & retval <= 57){			//recibo en ASCII
		    retval -= 48;
		    if(retval == 0){
		    	n = 10;
		    }else{
		        n = retval;
		    }
		    xQueueSend( xFilterQueue, &n, portMAX_DELAY );
		}		
	}
}

// envia el mensaje indicado caracter por caracter por el puerto serie 
static void uartPrint(char * msg,uint8_t len)
{
	//recoro el msg caracter por caracter y lo transmito
	for(uint8_t i=0;i<len;i++){
		UARTCharPut(UART0_BASE,(unsigned)msg[i]);
	}
	//imprimo un return carriage al final
	UARTCharPut(UART0_BASE,'\n');
}

/*--------------------------------SENSOR------------------------------------------*/

//sensor de temperatura, aumenta de 0 a 127 y luego disminuye
static void vSensorTask(void *pvParameters)
{
	int temp = 1;
	int cont = 0;

	// UBaseType_t uxHighWaterMark_Stack;
	// char aux_watermark[16];

	for (;;)
	{
		if (cont)
		{
			temp++;
			if (temp >= 127)
			{
				cont = 0;
			}
		}
		else
		{
			temp--;
			if (temp <= 16)
			{
				cont = 1;
			}
		}

		/* Send message */
		xQueueSend(xValueQueue, &temp, portMAX_DELAY);
		vTaskDelay(10);

		// uxHighWaterMark_Stack = uxTaskGetStackHighWaterMark(NULL); //devuelve el espacio que quedo vacio en la pila
		// uartPrint( longToChar(uxHighWaterMark_Stack, aux_watermark, 10) , strlen(longToChar(uxHighWaterMark_Stack, aux_watermark, 10)) );
	}
}

/*--------------------------------FILTER------------------------------------------*/

//Filtro pasabajo, se calcula un pormedio de las ultimas N mediciones del sensor. N es ingresado por UART, inicializado en 1
static void vFilterTask(void *pvParameters)
{
	unsigned int msg_rcv = 0;
	unsigned int avg = 0;
	unsigned int new_N;
	unsigned int array[MAX_VALUE_N] = {0};
	unsigned int aux[MAX_VALUE_N] = {0};
	N = 1;
	char *val;

	// UBaseType_t uxHighWaterMark_Stack;
	// char aux_watermark[16];

	for (;;)
	{
		avg = 0;
		/* Wait for a message to arrive. */
		xQueueReceive(xValueQueue, &msg_rcv, portMAX_DELAY);
		if(xQueueReceive( xFilterQueue, &new_N, 0 ) == pdTRUE){
			N = new_N;
		}	

		for (int i = 0; i < N; i++)
		{
			aux[i] = array[i];
		}
		for (int i = 0; i < (N - 1); i++)
		{
			array[i + 1] = aux[i];
		}
		array[0] = msg_rcv;

		for (int i = 0; i < N; i++)
		{
			avg += array[i];
		}
		avg = avg / N;

		/* Send message */
		xQueueSend(xPrintQueue, &avg, portMAX_DELAY);

		// uxHighWaterMark_Stack = uxTaskGetStackHighWaterMark(NULL); //devuelve el espacio que quedo vacio en la pila
		// uartPrint( longToChar(uxHighWaterMark_Stack, aux_watermark, 10) , strlen(longToChar(uxHighWaterMark_Stack, aux_watermark, 10)) );
	}
}
	
/*--------------------------------GRAFIC------------------------------------------*/
//Grafica los valores de temperatura en el tiempo, en el LCD se muestra el valor de N actual y el promedio de las mediciones
static void vGraficTask(void *pvParameters)
{
	unsigned int avg_rcv = 0;
	unsigned char pixel[128] = {0};
	uint8_t value;
	char *N_char = "10";
	char *avg_char;
	N = 1;

	// UBaseType_t uxHighWaterMark_Stack;
	// char aux_watermark[16];

	for (;;)
	{
		/* Wait for a message to arrive. */
		xQueueReceive(xPrintQueue, &avg_rcv, portMAX_DELAY);
		avg_char = itoa(avg_rcv);	

		N_char = itoa(N);
		
		value = (uint8_t)avg_rcv / 8;

		if (value < 8)
		{
			pixel[127] = (~(0b111111111 >> (value)));
			pixel[63] = 0;
		}
		else
		{
			pixel[63] = 0b11111111 << (16 - value);
			pixel[127] = 0b11111111;
		}

		for (int i = 0; i < 128; i++)
		{
			if (i != 63 && i != 127)
			{
				pixel[i] = pixel[i + 1];
			}
		}

		/* Write the message to the LCD. */
		OSRAMClear();
		OSRAMStringDraw(avg_char, 0, 1);
		OSRAMStringDraw(N_char, 0, 0);
		OSRAMImageDraw(pixel, 16, 0, 64, 2);
		
		// uxHighWaterMark_Stack = uxTaskGetStackHighWaterMark(NULL); //devuelve el espacio que quedo vacio en la pila
		// uartPrint( longToChar(uxHighWaterMark_Stack, aux_watermark, 10) , strlen(longToChar(uxHighWaterMark_Stack, aux_watermark, 10)) );
	}
}

/*--------------------------------TOP---------------------------------------------*/

// Se analiza la informacion de las tareas en uso 
static void vTopTask(void *pvParameters )
{
	char pcMessage[150];
	// UBaseType_t uxHighWaterMark_Stack;
	// char aux_watermark[16];

	for( ;; )
	{	
		vOwnTaskGetRunTimeStats(pcMessage);

		to_terminal(pcMessage);

		vTaskDelay( pdMS_TO_TICKS(3000) );
		
		// uxHighWaterMark_Stack = uxTaskGetStackHighWaterMark(NULL); //devuelve el espacio que quedo vacio en la pila
		// uartPrint( longToChar(uxHighWaterMark_Stack, aux_watermark, 10) , strlen(longToChar(uxHighWaterMark_Stack, aux_watermark, 10)) );
	}

}

//esta funcion carga en el string info de la cantidad de tiempo de procesamiento que ha utilizado cada tarea, para imprimir por UART
// Tiempo absoluto: Este es el 'tiempo' total que la tarea se ha estado ejecutando realmente (el tiempo total que la tarea ha sido en el estado de ejecución
void vOwnTaskGetRunTimeStats(signed char *pcWriteBuffer){
	TaskStatus_t *pxTaskStatusArray;
	volatile UBaseType_t uxArraySize, x;
	uint32_t ulTotalRunTime, ulStatsAsPercentage;
	char aux[16];

	/* Make sure the write buffer does not contain a string. */
	*pcWriteBuffer = 0x00;

	/* Take a snapshot of the number of tasks in case it changes while this
	function is executing. */
	uxArraySize = uxTaskGetNumberOfTasks();

	/* Allocate a TaskStatus_t structure for each task.  An array could be
	allocated statically at compile time. */
	pxTaskStatusArray = pvPortMalloc(uxArraySize*sizeof(TaskStatus_t));

	if(pxTaskStatusArray != NULL){
		/* Generate raw status information about each task. */
		uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

		/* For percentage calculations. */
		ulTotalRunTime /= 100UL;

		/* Avoid divide by zero errors. */
		if(ulTotalRunTime > 0){
			// strcat(pcWriteBuffer, "Name\t\tRunTime\t\t%RunTime\n");
			strcat(pcWriteBuffer, "====================================\n");
			strcat(pcWriteBuffer, "Task	Abs Time\n");
			strcat(pcWriteBuffer, "************************************\n");
			/* For each populated position in the pxTaskStatusArray array,
			format the raw data as human readable ASCII data. */
			for(x = 0; x<uxArraySize; x++){
				/* What percentage of the total run time has the task used?
				This will always be rounded down to the nearest integer.
				ulTotalRunTimeDiv100 has already been divided by 100. */
				ulStatsAsPercentage = pxTaskStatusArray[ x ].ulRunTimeCounter / ulTotalRunTime;

				if(ulStatsAsPercentage > 0UL){
					strcat(pcWriteBuffer, pxTaskStatusArray[x].pcTaskName);
					strcat(pcWriteBuffer, "\t");
					strcat(pcWriteBuffer, longToChar(pxTaskStatusArray[ x ].ulRunTimeCounter, aux, 10));
					strcat(pcWriteBuffer, "\n");
				}
				else{
					/* If the percentage is zero here then the task has
					consumed less than 1% of the total run time. */
					strcat(pcWriteBuffer, pxTaskStatusArray[x].pcTaskName);
					strcat(pcWriteBuffer, "\t");
					strcat(pcWriteBuffer, longToChar(pxTaskStatusArray[ x ].ulRunTimeCounter, aux, 10));
					strcat(pcWriteBuffer, "\n");
				}

				pcWriteBuffer += strlen( ( char * ) pcWriteBuffer );
			}
		}
		/* The array is no longer needed, free the memory it consumes. */
		vPortFree( pxTaskStatusArray );
	}
}

/*--------------------------------AUX------------------------------------------*/

/*casteo de int a char*/
char *itoa(int val)
{
	static char buf[32] = {0};

	int i = 30;

	for (; val && i; --i, val /= 10)

		buf[i] = "0123456789abcdef"[val % 10];

	return &buf[i + 1];
}

/*caster de long a char*/
char* longToChar(unsigned long value, char *ptr, int base)
{
	unsigned long t = 0, res = 0;
	unsigned long tmp = value;
	int count = 0;

	if (NULL == ptr) {
	return NULL;
	}

	if (tmp == 0) {
	count++;
	}

	while(tmp > 0) {
	tmp = tmp/base;
	count++;
	}

	ptr += count;

	*ptr = '\0';

	do {

	res = value - base * (t = value / base);
	if (res < 10) {
		* -- ptr = '0' + res;
	}
	else if ((res >= 10) && (res < 16)) {
		* --ptr = 'A' - 10 + res;
	}

	} while ((value = t) != 0);

	return(ptr);
}

void intToString(int value, char *aux){
	sprintf(aux, "%d\n", value);
}

static void to_terminal(char* s){
	UARTIntDisable(UART0_BASE, UART_INT_TX);
	{
		pcNextChar = s;

		/* Send the first character. */
		if(!(HWREG(UART0_BASE + UART_O_FR) & UART_FR_TXFF)){
			HWREG(UART0_BASE + UART_O_DR) = *pcNextChar;
		}

		pcNextChar++;
	}

	UARTIntEnable(UART0_BASE, UART_INT_TX);
}
