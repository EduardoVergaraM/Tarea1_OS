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
    int tiempo;                 
    Estado estado;
    int cont;                   // dependencias pendientes (indegree)
    int *sucesores;             
    int num_sucesores, cap_sucesores;
    char insumos[512];          // mensajes recibidos de sus dependencias
    pid_t pid;
    int fd_resultado;           // extremo de lectura del pipe hijo -> padre
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
//probar fallos al modificar el valor de PROB_FALLO
#define PROB_FALLO 5
#define MSJ_LEN 64     

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

                // Dos pipes por actividad:
                //   p_in : padre -> hijo  (insumos de sus dependencias)
                //   p_out: hijo  -> padre (mensaje al terminar)
                int p_in[2], p_out[2];
                if (pipe(p_in) == -1) {
                    if (procesos_activos > 0) break;          // sin recursos: esperar a que termine alguien
                    perror("pipe"); exit(1);
                }
                if (pipe(p_out) == -1) {
                    close(p_in[0]); close(p_in[1]);
                    if (procesos_activos > 0) break;
                    perror("pipe"); exit(1);
                }

                fflush(stdout);   // evita que el hijo herede texto pendiente de imprimir
                pid_t pid = fork();

                if (pid == 0) {
                    
                    signal(SIGINT, SIG_DFL);
                    close(p_in[1]);
                    close(p_out[0]);

                    // Leer los insumos que envió el padre
                    char insumos[sizeof planificacion[i].insumos];
                    ssize_t n, total = 0;
                    while ((n = read(p_in[0], insumos + total, sizeof insumos - 1 - total)) > 0)
                        total += n;
                    insumos[total] = '\0';
                    close(p_in[0]);

                    if (total > 0) {
                        printf("%s recibe: %s\n", planificacion[i].nombre, insumos);
                        fflush(stdout);
                    }

                    int status_salida = ejecutar_actividad(i);

                    // Si terminó bien, avisa su insumo al padre
                    if (status_salida == 0) {
                        char msj[MSJ_LEN];
                        int len = snprintf(msj, sizeof msj, "%.50s listo", planificacion[i].nombre);
                        if (write(p_out[1], msj, len) < 0) { /* nada que hacer */ }
                    }
                    close(p_out[1]);
                    _exit(status_salida);

                } else if (pid > 0) {
                
                    close(p_in[0]);
                    close(p_out[1]);

                    // Enviar los insumos acumulados y cerrar (el hijo recibe EOF)
                    size_t len = strlen(planificacion[i].insumos);
                    if (len > 0 && write(p_in[1], planificacion[i].insumos, len) < 0)
                        perror("write");
                    close(p_in[1]);

                    planificacion[i].fd_resultado = p_out[0];
                    planificacion[i].pid = pid;
                    planificacion[i].estado = EJECUTANDO;
                    procesos_activos++;
                    printf("INICIO %s (%s) [%d/%d]\n", planificacion[i].id, planificacion[i].nombre, procesos_activos, K);

                } else {
                    close(p_in[0]); close(p_in[1]);
                    close(p_out[0]); close(p_out[1]);
                    if (procesos_activos > 0) break;          // reintentar cuando se libere un cupo
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

                        // Leer el mensaje que dejó el hijo y cerrar su pipe
                        char msj[MSJ_LEN];
                        ssize_t n = read(planificacion[i].fd_resultado, msj, sizeof msj - 1);
                        msj[n > 0 ? n : 0] = '\0';
                        close(planificacion[i].fd_resultado);

                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            planificacion[i].estado = TERMINADA;
                            printf("FIN %s OK\n", planificacion[i].id);
                            for (int j = 0; j < planificacion[i].num_sucesores; j++) {
                                Proceso *suc = &planificacion[planificacion[i].sucesores[j]];

                                // Guardar el insumo en el sucesor (acotado al tamaño del buffer)
                                size_t usado = strlen(suc->insumos);
                                snprintf(suc->insumos + usado, sizeof suc->insumos - usado,
                                         "%s%s", usado ? " | " : "", msj);

                                suc->cont--;
                                if (suc->cont == 0) suc->estado = LISTA;
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

    