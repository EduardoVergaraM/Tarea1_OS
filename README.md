# Tarea 1 — El Planificador Dieciochero

**Sistemas Operativos — Universidad Diego Portales**
Integrantes: Eduardo Vergara ; Francisco Olmos.

Simulador que ejecuta un plan de actividades modelado como un DAG, donde cada actividad corre como un proceso hijo, con un máximo de K procesos simultáneos.

## Compilación y ejecución

```bash
gcc -Wall -Wextra -std=c17 Tarea1.c -o Tarea1
./Tarea1 plan_X.txt K
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

- **Modelado del DAG:** cada actividad guarda `cont` (dependencias pendientes) y `sucesores` (quiénes dependen de ella). Una actividad pasa a LISTA cuando `cont` llega a 0. La lista de sucesores es dinámica (`realloc`), porque una actividad puede tener muchos dependientes.
- **Dos pasadas en el parseo:** permiten que una dependencia aparezca en el archivo después de la actividad que la usa.
- **Control de concurrencia:** el padre solo lanza procesos mientras `procesos_activos < K`.
- **Sin busy waiting:** el padre espera con `wait()` bloqueante y solo despierta cuando termina un hijo.
- **Sin race conditions:** solo el padre modifica la estructura del planificador. Los hijos únicamente simulan su trabajo y terminan con un código de salida. No se usan hilos.
- **Aislamiento de errores:** si un hijo termina con código distinto de 0, la actividad se marca FALLIDA y `abortar_rama` aborta solo sus descendientes. El resto del plan continúa. Los fallos se simulan con `PROB_FALLO` (5% por defecto; 0 los desactiva). Cada hijo usa su propia semilla (`time ^ getpid`) para que los fallos sean independientes.
- **Ctrl+C (Seremi):** el handler solo activa una bandera `volatile sig_atomic_t`, porque dentro de un handler solo se deben hacer operaciones seguras. `sigaction` se instala sin `SA_RESTART`, así `wait()` se interrumpe (`EINTR`) y el padre reacciona de inmediato. Luego se envía `SIGTERM` a los hijos activos y se recogen todos con `wait()` para no dejar zombis. Los hijos restauran el comportamiento por defecto de SIGINT.
- **Paso de mensajes (pipes):**

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

