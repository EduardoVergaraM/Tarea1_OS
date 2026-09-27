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


int main(int argc, char **argv){





    return 0;
}
