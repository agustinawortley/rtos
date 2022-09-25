FreeRTOS usa un scheduler del tipo Round Robin con niveles de prioridad
Si tiene 3 tareas con el mismo nivel de priodidad, le asignada un mismo t de ejecucion a c/u
Si hay alguna tarea con mayor nivel de prioridad, esa va a estar ejecutandose (si es q ya esta lista), y hasta que no se bloquee o termine no van a tener tiempo de procesador a las demas tareas

xTaskCreate( vPrintTask, "Print", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, NULL );

vPrintTask -> funcion q va a ejecutar 
"Print" -> nombre de la tarea
configMINIMAL_STACK_SIZE -> stack, cada tarea tiene un stack (con vbles locales, guardan direcciones, argumentos q se pasan por funciones), hay q darle un tamaño, y debe ser calculado para no tener un stack overflow
usar watermark para saber el valor, debo ejecutar el codigo y buscar el resultado
https://www.freertos.org/uxTaskGetStackHighWaterMark.html
Para ver cual task hace stackoverflow uso 
https://www.freertos.org/Stacks-and-stack-overflow-checking.html


freertosConfig

total_heap_size 


las Queues son recursos de freertos y sirven como herramienta de sicronizacion
si la cola esta vacia, no puedo sacar nada, 
si la cola esta llena, no puede entrar nada
Las queue resuelven estos problemas
Hay que crearlas, como las tasck

