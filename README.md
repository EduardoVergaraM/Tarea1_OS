# Tarea 1 — El Planificador Dieciochero

**Sistemas Operativos — Universidad Diego Portales**
Integrantes: Eduardo Vergara ; Francisco Olmos.

Simulador que ejecuta un plan de actividades modelado como un DAG, donde cada actividad corre como un proceso hijo, con un máximo de K procesos simultáneos.

## Funciones Implementadas

### `cargar_plan(const char *ruta)`
Se encarga de procesar el archivo de texto y construir el Grafo Acíclico Dirigido (DAG) en memoria. 
*   **Implementación:** Utiliza un algoritmo de dos pasadas. La primera lee los datos y asigna tiempos aleatorios si es necesario. La segunda resuelve las dependencias estructurando las tareas.
*   **Diseño del DAG:** Para cada tarea, define dinámicamente un arreglo de `sucesores` (usando `realloc`) para propagar los cambios de estado hacia el futuro. Además, inicializa un arreglo estático de `predecesores` que servirá más adelante para optimizar la lectura de los pipes sin necesidad de buscar en todo el arreglo principal.

### `simular_planificador(int K)`
Es la función  más importante del simulador. Mantiene un ciclo activo hasta que todas las tareas del plan han sido procesadas (ya sea terminadas, fallidas o abortadas). En esta función recae la mayor parte de la sincronización y comunicación:
*   **Control de Concurrencia:** Recorre el arreglo buscando tareas en estado `LISTA` y ejecuta `fork()` garantizando siempre que la variable local `procesos_activos` no supere el límite `K`. Al ser una variable local controlada por el padre, se evitan las condiciones de carrera.
*   **Comunicación IPC (Pipes):** Antes de hacer el `fork()`, el padre crea el *pipe* de la tarea. El hijo lee directamente de los *pipes* de sus predecesores, arma su "insumo" y, si es exitoso, escribe su mensaje para el futuro. 
*   **Gestión de Memoria y Fugas:** El padre maneja un contador de `lecturas_pendientes` por cada *pipe*. Una vez que todos los descendientes de una tarea han nacido y leído, el padre cierra el descriptor de archivo, evitando saturar los límites del kernel operativo.
*   **Gestión Macro de Errores y Espera:** Utiliza `wait(&status)` de forma bloqueante, evitando el *busy-waiting*. Cuando un proceso retorna, esta función detecta mediante `WEXITSTATUS` si el hijo terminó correctamente o falló. Si detecta un fallo, actúa como el controlador macro: marca la tarea como `FALLIDA` y gatilla la función `abortar_rama()`.

### `ejecutar_actividad(int i)`
Representa la carga de trabajo real que ejecuta el proceso hijo.
*   **Aislamiento Micro:** Simula el paso del tiempo con `usleep()` y somete el proceso a una probabilidad de fallo aleatoria (ajustada con `PROB_FALLO` al 5%). Cada proceso utiliza su propia semilla (`time ^ getpid`) para garantizar que la probabilidad sea independiente y aislar los errores a nivel de ejecución interna.

### `abortar_rama(int idx)`
Es la función responsable del aislamiento de los errores en el DAG.
*   **Implementación:** Es una función recursiva. Cuando `simular_planificador` detecta un fallo y la invoca, esta recorre el arreglo de `sucesores` de la tarea afectada, marcándolos iterativamente como `ABORTADA`. Esto garantiza que una rama defectuosa se cancele limpiamente, permitiendo que las ramas independientes del plan general continúen su ejecución normal sin que el simulador completo se detenga.

### Manejo de Señales (Inspección de la Seremi)
Implementado a través del conjunto de funciones `instalar_sigint()`, `manejar_sigint()` e `inspeccion_seremi()`:
*   **Manejador Seguro:** Al recibir `SIGINT` (Ctrl+C), el sistema simplemente altera una bandera asíncrona (`volatile sig_atomic_t seremi`), que es la única operación 100% segura dentro del contexto de interrupciones del kernel.
*   **Apagado Limpio:** En el siguiente ciclo del planificador, al detectar la bandera activa, `inspeccion_seremi()` se encarga de enviar `SIGTERM` vía `kill()` a los procesos en ejecución, cancelar el resto, y usar `wait()` para recoger a todos los hijos muertos, garantizando que el simulador termine sin dejar procesos zombis.

## Compilación y ejecución

```bash
gcc -Wall -Wextra -std=c17 planificador.c -o planificador
./planificador plan_X.txt K

```

- `plan_chico.txt`: archivo con 10 actividades.
- `plan_grande.txt`: archivo con 100000 actividades.
- `K`: número máximo de procesos hijos simultáneos (entero > 0).

## Formato del archivo

```
ID : nombre : tiempo_ms : dep1, dep2, ...
```

Si `tiempo_ms` está vacío, se asigna un valor aleatorio entre 100 y 5000 ms.

## Decisiones de diseño

- **Modelado del DAG:** cada actividad guarda `cont` (dependencias pendientes) y `sucesores` (quiénes dependen de ella) y un arreglo de predecesores. Una actividad pasa a LISTA cuando `cont` llega a 0. La lista de sucesores es dinámica (`realloc`), porque una actividad puede tener muchos dependientes.
- **Dos pasadas en el parseo:** permiten que una dependencia aparezca en el archivo después de la actividad que la usa.
- **Control de concurrencia:** El padre gestiona el límite de tareas simultaneas mediante una variable local `procesos_activos` dentro del ciclo simulación. Solo lanza nuevos procesos `fork` mientras procesos activos mientras `procesos_activos < K`.
- **Sin busy waiting:** el padre espera con `wait()` bloqueante y solo despierta cuando termina un hijo.
- **Sin race conditions:** solo el padre modifica la estructura del planificador. Los hijos únicamente simulan su trabajo y terminan con un código de salida. No se usan hilos.
- **Aislamiento de errores:** si un hijo termina con código distinto de 0, la actividad se marca FALLIDA y `abortar_rama` aborta solo sus descendientes. El resto del plan continúa. Los fallos se simulan con `PROB_FALLO` (5% por defecto; 0 los desactiva). Cada hijo usa su propia semilla (`time ^ getpid`) para que los fallos sean independientes.
- **Ctrl+C (Seremi):** el handler solo activa una bandera `volatile sig_atomic_t`, porque dentro de un handler solo se deben hacer operaciones seguras. `sigaction` se instala sin `SA_RESTART`, así `wait()` se interrumpe (`EINTR`) y el padre reacciona de inmediato. Luego se envía `SIGTERM` a los hijos activos y se recogen todos con `wait()` para no dejar zombis. Los hijos restauran el comportamiento por defecto de SIGINT.
- **Paso de mensajes (pipes):** cada actividad usa dos pipes. Al terminar, el hijo envía su mensaje al padre por p_out. El padre lo agrega a los insumos de cada sucesor y se los envía por p_in al lanzarlo. Usar al padre como intermediario mantiene abiertos solo unos K pipes a la vez, evitando agotar los descriptores con 10000 actividades.

## Prueba de estrés

Plan de 10000 actividades con K = 100 

`PROB_FALLO` = 0: (PruebaEstres_1.txt)

| OK | Fallidas | Abortadas | Total | Máx. simultáneos | Tiempo |
|---|---|---|---|---|---|
| 4687 | 268 | 5045 | 10000 | 100/100 | 4min y 19 segundos |

`PROB_FALLO` = 5: (PruebaEstres_2.txt)

| OK | Fallidas | Abortadas | Total | Máx. simultáneos | Tiempo |
|---|---|---|---|---|---|
| 4031 | 205 | 5764 | 10000 | 100/100 | 1minuto y 52 segundos |

