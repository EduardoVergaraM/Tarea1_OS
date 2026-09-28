
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
//Ver despues que hace en si !!!!!!!
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s N archivo.txt\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "N debe ser > 0\n");
        return 1;
    }
    FILE *f = fopen(argv[2], "w");
    if (!f) {
        perror("fopen");
        return 1;
    }
    srand(time(NULL));

    for (int i = 1; i <= n; i++) {
        fprintf(f, "%d : actividad_%d : ", i, i);

        // 10% sin tiempo (el planificador debe asignarlo aleatorio)
        if (rand() % 10 != 0)
            fprintf(f, "%d", 100 + rand() % 4901);
        fprintf(f, " :");

        // Hasta 3 dependencias, solo con IDs menores -> nunca hay ciclos
        int max_deps = (i - 1 < 3) ? i - 1 : 3;
        int ndeps = max_deps ? rand() % (max_deps + 1) : 0;
        int usadas[3];
        int k = 0;
        while (k < ndeps) {
            int d = 1 + rand() % (i - 1);
            int repetida = 0;
            for (int j = 0; j < k; j++)
                if (usadas[j] == d) repetida = 1;
            if (repetida) continue;
            usadas[k] = d;
            fprintf(f, "%s %d", k ? "," : "", d);
            k++;
        }
        fprintf(f, "\n");
    }
    fclose(f);
    printf("Generado %s con %d actividades\n", argv[2], n);
    return 0;
}