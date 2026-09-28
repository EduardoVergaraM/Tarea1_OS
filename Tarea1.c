#define _DEFAULT_SOURCE
#include <stdio.h> //abrir archivo, printf, perror
#include <stdlib.h> //Malloc, free, exit / memoria dinamica
#include <string.h> //strdup, strcmp, strlen, strtok / separador de texto
#include <unistd.h> //fork, pipe, close, read, write
#include <sys/wait.h> //waitpid 
#include <ctype.h> //isspace /quita espacios de un texto
#include <time.h> //time(NULL) para semilla de rand
#include <sys/types.h> //pid_t



typedef enum { BLOQUEADA, LISTA, EJECUTANDO, TERMINADA, FALLIDA, ABORTADA } Estado;

typedef struct {
    char id[32];
    char nombre[128];
    int tiempo;                 // ms
    Estado estado;
    int cont;                   // dependencias pendientes (indegree)
    int *sucesores;             // índices de las tareas que dependen de esta -> que pasa si lo hao estatico????
    int num_sucesores, cap_sucesores;
    char insumos[512];          // mensajes recibidos de sus dependencias
    pid_t pid;
    int fd_pipe[2];
    char *deps_texto;           // temporal: dependencias como texto hasta resolverlas
} Proceso;

Proceso planificacion[10000];
int total_tareas = 0;

static char *trim(char *s) { //quita los espacios del inicio y del final de un texto, es realmente necesario???
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = '\0';
    return s;
}

// Busca el índice de una tarea por su ID (texto)
static int buscar_indice(const char *id) {
    for (int i = 0; i < total_tareas; i++)//->bsuqueda lineal 
        if (strcmp(planificacion[i].id, id) == 0) return i;
    return -1;
}

static void agregar_sucesor(Proceso *p, int idx) {
    if (p->num_sucesores == p->cap_sucesores) {
        p->cap_sucesores = p->cap_sucesores ? p->cap_sucesores * 2 : 4;
        int *nuevo = realloc(p->sucesores, p->cap_sucesores * sizeof(int));
        if (!nuevo) { perror("realloc"); exit(1); }
        p->sucesores = nuevo;
    }
    p->sucesores[p->num_sucesores++] = idx;
}

// Lee el archivo y construye el DAG. Retorna 0 si todo bien, -1 si hay error.
int cargar_plan(const char *ruta) {
    FILE *f = fopen(ruta, "r");
    if (!f) { perror(ruta); return -1; }

    char linea[4096];
    int nlinea = 0;

    // ---- Pasada 1: leer cada línea ----
    while (fgets(linea, sizeof linea, f)) {
        nlinea++;
        char *l = trim(linea);
        if (*l == '\0') continue;                       // línea vacía

        if (total_tareas == 10000) {
            fprintf(stderr, "Error: más de %d actividades\n", 10000);
            fclose(f); return -1;
        }

        // Separar por ':' en máximo 4 campos
        char *campos[4];
        int n = 0;
        char *p = l;
        campos[n++] = p;
        while (n < 4 && (p = strchr(p, ':')) != NULL) {
            *p++ = '\0';
            campos[n++] = p;
        }

        Proceso *t = &planificacion[total_tareas]; //????
        memset(t, 0, sizeof *t); //deja tarea vacia
        t->pid = -1;

        snprintf(t->id, sizeof t->id, "%s", trim(campos[0]));
        snprintf(t->nombre, sizeof t->nombre, "%s", trim(campos[1]));

        char *tt = trim(campos[2]);
        if (*tt == '\0') {
            t->tiempo = 100 + rand() % 4901;            // aleatorio 100-5000 ms
        } else {
            char *fin;
            long v = strtol(tt, &fin, 10);
            if (*fin != '\0' || v < 0) {
                fprintf(stderr, "Error línea %d: tiempo inválido '%s'\n", nlinea, tt);
                fclose(f); return -1;
            }
            t->tiempo = (int)v;
        }

        t->deps_texto = strdup(n == 4 ? trim(campos[3]) : "");
        total_tareas++;
    }
    fclose(f);

    // ---- Pasada 2: resolver dependencias (pueden referirse a IDs que aparecen más abajo) ----
    for (int i = 0; i < total_tareas; i++) {
        Proceso *t = &planificacion[i];
        char *tok = strtok(t->deps_texto, ",");
        while (tok) {
            char *d = trim(tok);
            if (*d) {
                int j = buscar_indice(d);
                if (j == -1) {
                    fprintf(stderr, "Error: '%s' depende de '%s', que no existe\n", t->id, d);
                    return -1;
                }
                agregar_sucesor(&planificacion[j], i);
                t->cont++;
            }
            tok = strtok(NULL, ",");
        }
        free(t->deps_texto);
        t->deps_texto = NULL;
        t->estado = (t->cont == 0) ? LISTA : BLOQUEADA;
    }
    return 0;
}

// Marca como ABORTADA toda la descendencia de la tarea idx
int abortar_rama(int idx) {
    int abortadas = 0;
    for (int j = 0; j < planificacion[idx].num_sucesores; j++) {
        int s = planificacion[idx].sucesores[j];
        if (planificacion[s].estado == BLOQUEADA) {
            planificacion[s].estado = ABORTADA;
            printf("ABORTA %s (depende de %s)\n", planificacion[s].id, planificacion[idx].id);
            abortadas++;
            abortadas += abortar_rama(s);
        }
    }
    return abortadas;
}

//Funcion principal que se encarga de simular el planificador
void simular_planificador(int K) {
    int procesos_activos = 0;
    int tareas_finalizadas = 0; // Contador de tareas terminadas y abortadas

    while (tareas_finalizadas < total_tareas) {

        // Etapa donde el padre se encarga de asignar una tarea a un hijo y actualizar datos de variables en el arreglo del struct de Procesos
        for (int i = 0; i < total_tareas && procesos_activos < K; i++) { //condicion para que cada vez que se ejecute el ciclo, se verifiquen todas las tareas nuevamente y no se tengan mas procesos en ejecucion de los que nos permite k
            
            if (planificacion[i].estado == LISTA) {

                pid_t pid = fork();

                if (pid == 0) {

                    usleep(planificacion[i].tiempo * 1000); //simular tiempo de trabajo
                    _exit(0); 

                } else if (pid > 0) {
                    planificacion[i].pid = pid;            //Se asocia el pid del hijo a una tarea
                    planificacion[i].estado = EJECUTANDO;  //Se actualiza el estado
                    procesos_activos++;  
                    printf("INICIO %s (%s) [%d/%d]\n", planificacion[i].id, planificacion[i].nombre, procesos_activos, K);
                  // Se actualiza la cantidad de procesos activos
                } else {
                    perror("Error al hacer fork");
                    planificacion[i].estado = FALLIDA;
                    
                    tareas_finalizadas += 1 + abortar_rama(i);
                }
            }
        }

        // Etapa donde el padre verifica el estado de los procesos hijos
        if (procesos_activos > 0) {
            
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado > 0) {
                
                procesos_activos--; // Se libera un espacio para procesar otra tarea
                tareas_finalizadas++; // Se suma una tarea finalizada independientemente de si termino correctamente o no

                // Se actualiza el estado de la tarea y tareas dependientes
                for (int i = 0; i < total_tareas; i++) {
                    
                    if (planificacion[i].pid == pid_terminado) { //Se busca en el arreglo de procesos usando el pid asociado a la tarea previamente
                        
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { //Verifica que el hijo termino correctamente
                            
                            planificacion[i].estado = TERMINADA;
                            printf("FIN %s OK\n", planificacion[i].id);
                            for (int j = 0; j < planificacion[i].num_sucesores; j++) {
                                int sucesor = planificacion[i].sucesores[j];
                                
                                planificacion[sucesor].cont--;

                                // Se verifica si tareas que dependian de otras pueden pasar a estado listo
                                if (planificacion[sucesor].cont == 0) {
                                    planificacion[sucesor].estado = LISTA;
                                }
                            }
                            
                        } else { //Else para manejar casos donde el hijo no termino correctamente
                           planificacion[i].estado = FALLIDA;
                           printf("FIN %s FALLÓ\n", planificacion[i].id);
                           tareas_finalizadas += abortar_rama(i);
                        }
                        
                        break; 
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }
    int K = atoi(argv[2]);

    if (K <= 0) {
        fprintf(stderr, "K debe ser un entero > 0\n");
        return 1;
    }

    srand(time(NULL) ^ getpid());

    if (cargar_plan(argv[1]) != 0) return 1;

    // Prueba: mostrar lo que se cargó --> sacar al finallllll!!!!!!!!!!!!!!!!
    printf("Cargadas %d actividades (K = %d)\n", total_tareas, K);
    for (int i = 0; i < total_tareas; i++) {
        Proceso *t = &planificacion[i];
        printf("%-4s %-16s %5d ms  deps pendientes: %d  sucesores: [",
               t->id, t->nombre, t->tiempo, t->cont);
        for (int s = 0; s < t->num_sucesores; s++)
            printf("%s%s", s ? ", " : "", planificacion[t->sucesores[s]].id);
        printf("]\n");
    }

    simular_planificador(K);    

    for (int i = 0; i < total_tareas; i++) free(planificacion[i].sucesores);
    return 0;
}