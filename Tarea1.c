#define _DEFAULT_SOURCE
#include <stdio.h> //abrir archivo, printf, perror
#include <stdlib.h> //Malloc, free, exit / memoria dinamica
#include <string.h> //strdup, strcmp, strlen, strtok / separador de texto
#include <unistd.h> //fork, pipe, close, read, write
#include <sys/wait.h> //waitpid 
#include <ctype.h> //isspace /quita espacios de un texto
#include <time.h> //time(NULL) para semilla de rand
#include <sys/types.h> //pid_t
#include <signal.h> //sigaction, kill
#include <errno.h>  //errno, EINTR

typedef enum { BLOQUEADA, LISTA, EJECUTANDO, TERMINADA, FALLIDA, ABORTADA } Estado;

typedef struct {
    char id[32];
    char nombre[128];
    int tiempo;                 // ms
    Estado estado;
    int cont;                   // dependencias pendientes (indegree)
    int *sucesores;             
    int num_sucesores, cap_sucesores;
    int predecesores[50];
    int num_predecesores;
    int lecturas_pendientes;
    char insumos[512];          // mensajes recibidos de sus dependencias
    pid_t pid;
    int fd_pipe[2];
    char *deps_texto;           // temporal: dependencias como texto hasta resolverlas
} Proceso;

Proceso planificacion[10000];
int total_tareas = 0;

static char *trim(char *s) { 
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = '\0';
    return s;
}

static int buscar_indice(const char *id) {
    for (int i = 0; i < total_tareas; i++)
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

int cargar_plan(const char *ruta) {
    FILE *f = fopen(ruta, "r");
    if (!f) { perror(ruta); return -1; }

    char linea[4096];
    int nlinea = 0;

    while (fgets(linea, sizeof linea, f)) {
        nlinea++;
        char *l = trim(linea);
        if (*l == '\0') continue;                       

        if (total_tareas == 10000) {
            fprintf(stderr, "Error: más de %d actividades\n", 10000);
            fclose(f); return -1;
        }

        char *campos[4];
        int n = 0;
        char *p = l;
        campos[n++] = p;
        while (n < 4 && (p = strchr(p, ':')) != NULL) {
            *p++ = '\0';
            campos[n++] = p;
        }

        Proceso *t = &planificacion[total_tareas]; 
        memset(t, 0, sizeof *t); 
        t->pid = -1;

        snprintf(t->id, sizeof t->id, "%s", trim(campos[0]));
        snprintf(t->nombre, sizeof t->nombre, "%s", trim(campos[1]));

        char *tt = trim(campos[2]);
        if (*tt == '\0') {
            t->tiempo = 100 + rand() % 4901;            
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
                
                //El hijo anota a su predecesor
                t->predecesores[t->num_predecesores++] = j; 
                t->cont++;
            }
            tok = strtok(NULL, ",");
        }
        free(t->deps_texto);
        t->deps_texto = NULL;
        t->estado = (t->cont == 0) ? LISTA : BLOQUEADA;
    }

    // 
    for(int i = 0; i < total_tareas; i++){ 
        planificacion[i].lecturas_pendientes = planificacion[i].num_sucesores;
    }

    return 0;
}

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

#define PROB_FALLO 5 

int ejecutar_actividad(int i) {
    srand(time(NULL) ^ getpid());            
    usleep(planificacion[i].tiempo * 1000);  
    if (rand() % 100 < PROB_FALLO)
        return 1;                            
    return 0;                                
}

volatile sig_atomic_t seremi = 0;   

void manejar_sigint(int sig) {
    (void)sig;
    seremi = 1;                     
}

void instalar_sigint(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = manejar_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                
    sigaction(SIGINT, &sa, NULL);
}

void inspeccion_seremi(void) {
    printf("\n¡Llegó la Seremi! Abortando todas las actividades...\n");
    for (int i = 0; i < total_tareas; i++) {
        if (planificacion[i].estado == EJECUTANDO) {
            kill(planificacion[i].pid, SIGTERM);
            planificacion[i].estado = ABORTADA;
            printf("ABORTA %s (en ejecución)\n", planificacion[i].id);
        } else if (planificacion[i].estado == LISTA || planificacion[i].estado == BLOQUEADA) {
            planificacion[i].estado = ABORTADA;
        }
    }
    while (wait(NULL) > 0);         
    printf("Planificador detenido.\n");
}

void simular_planificador(int K) {
    int procesos_activos = 0;
    int tareas_finalizadas = 0; 

    while (tareas_finalizadas < total_tareas) {
        
        if (seremi) { inspeccion_seremi(); return; } 

        for (int i = 0; i < total_tareas && procesos_activos < K; i++) { 
            
            if (planificacion[i].estado == LISTA) {

                //Creación del pipe
                if(pipe(planificacion[i].fd_pipe) == -1){
                    perror("Error al crear pipe");
                    exit(1);
                }

                pid_t pid = fork();

                if (pid == 0) {
                    // Restaurar comportamiento de señal en el hijo
                    signal(SIGINT, SIG_DFL);  

                    //Lectura de predecesores
                    close(planificacion[i].fd_pipe[0]);

                    planificacion[i].insumos[0] = '\0'; 
                    for(int j=0; j<planificacion[i].num_predecesores; j++){
                        int id_padre = planificacion[i].predecesores[j];
                        char buffer[128];
                        int bytes = read(planificacion[id_padre].fd_pipe[0], buffer, sizeof(buffer)-1);
                        
                        if(bytes>0){
                            buffer[bytes] = '\0';
                            strcat(planificacion[i].insumos, buffer);
                            strcat(planificacion[i].insumos, " | ");
                        }
                    }

                    if(strlen(planificacion[i].insumos) > 0){
                        printf("%s depende de: %s\n", planificacion[i].nombre, planificacion[i].insumos);
                    }

                    // Simulación de trabajo y fallo aleatorio
                    int status_salida = ejecutar_actividad(i);

                    // Solo escribe si la actividad terminó con éxito
                    if (status_salida == 0) {
                        char msj[128];
                        snprintf(msj, sizeof(msj), "Tarea %s", planificacion[i].id);
                        write(planificacion[i].fd_pipe[1], msj, strlen(msj));
                    }
                    
                    close(planificacion[i].fd_pipe[1]);

                    _exit(status_salida);

                } else if (pid > 0) {
                    
                    //Limpieza de descriptores
                    close(planificacion[i].fd_pipe[1]);

                    if(planificacion[i].lecturas_pendientes == 0){
                        close(planificacion[i].fd_pipe[0]);
                    }

                    for(int j = 0; j < planificacion[i].num_predecesores; j++){
                        int id_padre = planificacion[i].predecesores[j];
                        planificacion[id_padre].lecturas_pendientes--;

                        if(planificacion[id_padre].lecturas_pendientes == 0){
                            close(planificacion[id_padre].fd_pipe[0]);
                        }
                    }

                    planificacion[i].pid = pid;            
                    planificacion[i].estado = EJECUTANDO;  
                    procesos_activos++;  
                    printf("INICIO %s (%s) [%d/%d]\n", planificacion[i].id, planificacion[i].nombre, procesos_activos, K);
                } else {
                    perror("Error al hacer fork");
                    planificacion[i].estado = FALLIDA;
                    tareas_finalizadas += 1 + abortar_rama(i);
                }
            }
        }

        if (procesos_activos > 0) {
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado < 0 && errno == EINTR) continue;

            if (pid_terminado > 0) {
                procesos_activos--; 
                tareas_finalizadas++; 

                for (int i = 0; i < total_tareas; i++) {
                    if (planificacion[i].pid == pid_terminado) { 
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { 
                            planificacion[i].estado = TERMINADA;
                            printf("FIN %s OK\n", planificacion[i].id);
                            for (int j = 0; j < planificacion[i].num_sucesores; j++) {
                                int sucesor = planificacion[i].sucesores[j];
                                planificacion[sucesor].cont--;
                                if (planificacion[sucesor].cont == 0) {
                                    planificacion[sucesor].estado = LISTA;
                                }
                            }
                        } else { 
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
    int ok = 0, fallidas = 0, abortadas = 0;
    for (int i = 0; i < total_tareas; i++) {
        if (planificacion[i].estado == TERMINADA) ok++;
        else if (planificacion[i].estado == FALLIDA) fallidas++;
        else if (planificacion[i].estado == ABORTADA) abortadas++;
    }
    printf("\n=== RESUMEN ===\nOK: %d | FALLIDAS: %d | ABORTADAS: %d | TOTAL: %d\n",
    ok, fallidas, abortadas, total_tareas);
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

    instalar_sigint();
    
    simular_planificador(K);    

    for (int i = 0; i < total_tareas; i++) free(planificacion[i].sucesores);
    return 0;
}

/**
    // Prueba: mostrar lo que se cargó 
    printf("Cargadas %d actividades (K = %d)\n", total_tareas, K);
    for (int i = 0; i < total_tareas; i++) {
        Proceso *t = &planificacion[i];
        printf("%-4s %-16s %5d ms  deps pendientes: %d  sucesores: [",
               t->id, t->nombre, t->tiempo, t->cont);
        for (int s = 0; s < t->num_sucesores; s++)
            printf("%s%s", s ? ", " : "", planificacion[t->sucesores[s]].id);
        printf("]\n");
    }
    */
    