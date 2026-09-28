#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<string.h>
#include<time.h>
#include<sys/wait.h>   

//Estructura que define los diferentes estados que puede tener cada proceso creado por el padre
typedef enum{
    BLOQUEADA,
    LISTA,
    EJECUTANDO,
    TERMINADA,
    ABORTADA
} Estado;


//Estructura que representa cada proceso que va a crear el padre
typedef struct{
    char id[32];
    char nombre[128];
    int tiempo;

    Estado estado;
    int cont; //contador que almacena cantidad de procesos de los cuales depende actualmente este proceso para poder ser ejecutado
    int sucesores[50]; //Variable para guardar id de las tareas que dependen de la tarea actual
    int num_sucesores;
    pid_t pid;
    int fd_pipe[2]; //Para guardar y manejar los extremos del pipe del proceso

} Proceso;

Proceso planificacion[10000];
int total_tareas = 0;


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
                    exit(0); 

                } else if (pid > 0) {

                    planificacion[i].pid = pid;            //Se asocia el pid del hijo a una tarea
                    planificacion[i].estado = EJECUTANDO;  //Se actualiza el estado
                    procesos_activos++;                    // Se actualiza la cantidad de procesos activos
                } else {
                    perror("Error al hacer fork");
                    exit(1);
                }
            }
        }

        // Etapa donde el padre verifica el estado de los procesos hijos
        if (procesos_activos > 0) {
            
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado > 0) {
                
                procesos_activos--; // Se libera un espacio para procesar otra tarea
                tareas_finalizadas++; // Se suma una tarea finalizada independientemente de si terminó correctamente o no

                // Se actualiza el estado de la tarea y tareas dependientes
                for (int i = 0; i < total_tareas; i++) {
                    
                    if (planificacion[i].pid == pid_terminado) { //Se busca en el arreglo de procesos usando el pid asociado a la tarea previamente
                        
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { //Verifica que el hijo terminó correctamente
                            
                            planificacion[i].estado = TERMINADA;

                            for (int j = 0; j < planificacion[i].num_sucesores; j++) {
                                int sucesor = planificacion[i].sucesores[j];
                                
                                planificacion[sucesor].cont--;

                                // Se verifica si tareas que dependian de otras pueden pasar a estado listo
                                if (planificacion[sucesor].cont == 0) {
                                    planificacion[sucesor].estado = LISTA;
                                }
                            }
                            
                        } else { //Else para manejar casos donde el hijo no terminó correctamente
                            planificacion[i].estado = ABORTADA;
                            
                        }
                        
                        break; 
                    }
                }
            }
        }
    }
}


int main(int argc, char **argv){





    return 0;
}
