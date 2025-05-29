/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Grupo Delta (SAMUEL GANTIVA, CARLOS PINZON, SEBASTIAN ALVAREZ, JORGE OLAYA, DANIEL HOYOS)
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: receptor.c
#	Descripción: Implementación del proceso receptor que gestiona la base de datos de libros y 
#                procesa operaciones (préstamo, devolución, renovación) enviadas por el solicitante. 
#                Utiliza OpenMP para paralelismo y named pipes para comunicación.
#****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <omp.h>
#include <signal.h>
#include "receptor.h"

// Variables globales para el buffer
struct Operaciones buffer[BUFFER_TAM];
// Contador de operaciones en el buffer
int bufferCont = 0;
// Se usa para saber cuando se terminan las tareas
volatile sig_atomic_t terminar = 0;
// Variable para simular espera sin busy-waiting
int buffer_no_vacio = 0;
int buffer_no_lleno = 1;

// Función que lee la base de datos de libros desde un archivo de texto y la carga en memoria
int leerDB(char *nomArchivo, struct Libros *libros) {
    FILE *archivo = fopen(nomArchivo, "r");
    if (!archivo) {
        printf("Error al abrir el archivo %s\n", nomArchivo);
        exit(1);
    }

    char linea[256];
    int cont = 0;
    while (fgets(linea, sizeof(linea), archivo) && cont < MAX_LIBROS) {
        if (linea[0] == '\n' || linea[0] == '\0') continue;
        linea[strcspn(linea, "\n")] = 0;
        if (sscanf(linea, "%249[^,],%d,%d", libros[cont].nombre, &libros[cont].isbn, &libros[cont].numEj) != 3) {
            continue; // Saltar líneas inválidas
        }
        if (libros[cont].numEj <= 0 || libros[cont].numEj > MAX_EJEMPLAR) {
            printf("Número de ejemplares inválido para ISBN %d: %d\n", libros[cont].isbn, libros[cont].numEj);
            continue;
        }
        printf("Libro leído: %s, ISBN: %d, NumEj: %d\n", libros[cont].nombre, libros[cont].isbn, libros[cont].numEj);
        for (int i = 0; i < libros[cont].numEj && fgets(linea, sizeof(linea), archivo); i++) {
            linea[strcspn(linea, "\n")] = 0;
            struct Ejemplar *e = &libros[cont].ejemplares[i];
            char fecha_str[11];
            if (sscanf(linea, "%d%*c%*c%c%*c%10[^,]", &e->numero, &e->status, fecha_str) != 3) {
                printf("Error con la línea de ejemplar: %s\n", linea);
                continue;
            }
            int dia, mes, anio;
            if (sscanf(fecha_str, "%d-%d-%d", &dia, &mes, &anio) != 3) {
                printf("Error al parsear la fecha: %s\n", fecha_str);
                snprintf(e->fecha, sizeof(e->fecha), "01-01-2000");
            } else {
                snprintf(e->fecha, sizeof(e->fecha), "%02d-%02d-%04d", dia, mes, anio);
                printf("Ejemplar leído: Num: %d, Status: %c, Fecha: %s\n", e->numero, e->status, e->fecha);
            }
        }
        cont++;
    }
    fclose(archivo);
    return cont;
}

// Añade una operación al buffer compartido
void anadirBuffer(struct Operaciones *op) {
    int added = 0;
    while (!added) {
        #pragma omp critical(buffer_access)
        {
            if (bufferCont < BUFFER_TAM) {
                buffer[bufferCont++] = *op;
                buffer_no_lleno = (bufferCont < BUFFER_TAM);
                buffer_no_vacio = 1;
                added = 1;
            }
        }
        if (!added) {
            #pragma omp flush(bufferCont, buffer_no_lleno, buffer_no_vacio) // Especificar variables
            usleep(10000); // Reducir busy-waiting
        }
    }
}

// Lee y elimina una operación del buffer compartido
struct Operaciones leerBuffer() {
    struct Operaciones op = {'Q', "", 0, 0};
    int retrieved = 0;

    while (!retrieved) {
        #pragma omp critical(buffer_access)
        {
            if (bufferCont > 0) {
                op = buffer[--bufferCont];
                buffer_no_vacio = (bufferCont > 0);
                buffer_no_lleno = 1;
                retrieved = 1;
            } else if (terminar) {
                retrieved = 1; // Salir si se marcó terminar
            }
        }
        if (!retrieved) {
            #pragma omp flush(bufferCont, buffer_no_vacio, buffer_no_lleno, terminar) // Especificar variables
            usleep(10000); // Reducir busy-waiting
        }
    }
    return op;
}

// Envía una respuesta al solicitante a través de un pipe nombrado específico
void enviarRespuesta(int pid, const char *mensaje) {
    char pipe2[20];
    snprintf(pipe2, sizeof(pipe2), "pipe_%d", pid);
    int fd = -1, intentos = 5;

    while (intentos-- > 0) {
        fd = open(pipe2, O_WRONLY);
        if (fd >= 0) {
            break;
        }
        usleep(100000);
    }

    if (fd < 0) {
        printf("No se pudo abrir el pipe %s\n", pipe2);
        return;
    }

    if (write(fd, mensaje, strlen(mensaje) + 1) == -1) {
        printf("Error al escribir en el pipe %s\n", pipe2);
    }

    close(fd);
}

// Lee una operación enviada por el solicitante a través del pipe principal
int leerPipe(int fd, struct Operaciones *op, int verbose) {
    char buffer[256];
    int bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        return 0;
    }

    buffer[bytes] = '\0';

    if (sscanf(buffer, "%c,%249[^,],%d,%d", &op->tipo, op->nombre, &op->isbn, &op->pid) != 4) {
        printf("Formato inválido recibido: %s\n", buffer);
        return 0;
    }

    if (verbose) {
        printf("Recibido: tipo = %c, nombre = %s, isbn = %d, pid = %d\n", op->tipo, op->nombre, op->isbn, op->pid);
    }

    if (op->tipo == 'Q') {
        anadirBuffer(op);
        #pragma omp critical(terminar_access)
        {
            terminar = 1;
        }
        return 0;
    } else if (op->tipo == 'D' || op->tipo == 'R') {
        return 1;
    } else if (op->tipo == 'P') {
        return 2;
    }

    return 0;
}

// Procesa las operaciones de devolución y renovación que están en el buffer
void auxiliar1(struct Libros *libros, int numLibros) {
    while (1) {
        struct Operaciones op = leerBuffer();
        if (op.tipo == 'Q') {
            break;
        }
        int libro_encontrado = 0;
        for (int i = 0; i < numLibros; i++) {
            if (libros[i].isbn == op.isbn && strcmp(libros[i].nombre, op.nombre) == 0) {
                libro_encontrado = 1;
                int encontrado = 0;
                for (int j = 0; j < libros[i].numEj; j++) {
                    if (libros[i].ejemplares[j].status == 'P' && !encontrado) {
                        if (op.tipo == 'D') {
                            #pragma omp critical(libros_access)
                            {
                                libros[i].ejemplares[j].status = 'D';
                            }
                            printf("Devolución realizada del libro: ISBN %d, Ejemplar %d\n", op.isbn, libros[i].ejemplares[j].numero);
                            char respuesta[256];
                            snprintf(respuesta, sizeof(respuesta), "Devolución exitosa: ISBN %d, Ejemplar %d", op.isbn, libros[i].ejemplares[j].numero);
                            enviarRespuesta(op.pid, respuesta);
                            encontrado = 1;
                            break;
                        } else if (op.tipo == 'R') {
                            #pragma omp critical(libros_access)
                            {
                                char dia[3], mes[3], anio[5];
                                sscanf(libros[i].ejemplares[j].fecha, "%2s-%2s-%4s", dia, mes, anio);
                                int d = atoi(dia);
                                d += 7;
                                if (d > 30) {
                                    d -= 30;
                                    int m = atoi(mes);
                                    m++;
                                    if (m < 1 || m > 12) m = 1;
                                    snprintf(mes, sizeof(mes), "%02d", m);
                                }
                                if (d < 1 || d > 30) d = 1;
                                snprintf(dia, sizeof(dia), "%02d", d);
                                dia[2] = '\0';
                                mes[2] = '\0';
                                anio[4] = '\0';
                                snprintf(libros[i].ejemplares[j].fecha, 11, "%2s-%2s-%4s", dia, mes, anio);
                            }
                            printf("Renovación procesada: ISBN %d, Ejemplar %d, Nueva fecha: %s\n", op.isbn, libros[i].ejemplares[j].numero, libros[i].ejemplares[j].fecha);
                            char respuesta[256];
                            snprintf(respuesta, sizeof(respuesta), "Renovación exitosa: ISBN %d, Ejemplar %d", op.isbn, libros[i].ejemplares[j].numero);
                            enviarRespuesta(op.pid, respuesta);
                            encontrado = 1;
                            break;
                        }
                    }
                }
                if (!encontrado) {
                    char respuesta[256];
                    snprintf(respuesta, sizeof(respuesta), "Error: No se encontró un ejemplar prestado para ISBN %d", op.isbn);
                    enviarRespuesta(op.pid, respuesta);
                    printf("No se encontró un ejemplar prestado para ISBN %d\n", op.isbn);
                }
                break;
            }
        }
        if (!libro_encontrado) { // Usamos la variable para evitar el warning
            char respuesta[256];
            snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op.isbn);
            enviarRespuesta(op.pid, respuesta);
            printf("ISBN %d no encontrado\n", op.isbn);
        }
    }
}

// Maneja comandos interactivos del usuario (s para salir, r para generar reporte)
void auxiliar2(struct Libros *libros, int numLibros) {
    char comando[3];
    while (1) {
        if (scanf("%2s", comando) != 1) {
            while (getchar() != '\n');
            printf("Entrada inválida, utilice 's' para salir o 'r' para reporte\n");
            continue;
        }
        while (getchar() != '\n');
        if (strcmp(comando, "s") == 0) {
            #pragma omp critical(terminar_access)
            {
                terminar = 1;
            }
            break;
        } else if (strcmp(comando, "r") == 0) {
            printf("Reporte:\n");
            #pragma omp critical(libros_access)
            {
                for (int i = 0; i < numLibros; i++) {
                    for (int j = 0; j < libros[i].numEj; j++) {
                        printf("%c, %s, %d, %d, %s\n", libros[i].ejemplares[j].status, libros[i].nombre, libros[i].isbn, libros[i].ejemplares[j].numero, libros[i].ejemplares[j].fecha);
                    }
                }
            }
        } else {
            printf("Utilice solo 's' o 'r' si quiere acabar la ejecución o ver un reporte\n");
        }
    }
}

// Procesa una operación de préstamo, actualizando el estado de un ejemplar disponible
void prestamoProceso(struct Operaciones *op, struct Libros *libros, int numLibros) {
    int libroEncontrado = 0;
    for (int i = 0; i < numLibros; i++) {
        if (libros[i].isbn == op->isbn && strcmp(libros[i].nombre, op->nombre) == 0) {
            libroEncontrado = 1;
            int encontrado = 0;
            for (int j = 0; j < libros[i].numEj; j++) {
                if (libros[i].ejemplares[j].status == 'D') {
                    #pragma omp critical(libros_access)
                    {
                        libros[i].ejemplares[j].status = 'P';
                        char dia[3], mes[3], anio[5];
                        sscanf(libros[i].ejemplares[j].fecha, "%2s-%2s-%4s", dia, mes, anio);
                        int d = atoi(dia);
                        d += 7;
                        if (d > 30) {
                            d -= 30;
                            int m = atoi(mes);
                            m++;
                            if (m < 1 || m > 12) m = 1;
                            snprintf(mes, sizeof(mes), "%02d", m);
                        }
                        if (d < 1 || d > 30) d = 1;
                        snprintf(dia, sizeof(dia), "%02d", d);
                        dia[2] = '\0';
                        mes[2] = '\0';
                        anio[4] = '\0';
                        snprintf(libros[i].ejemplares[j].fecha, 11, "%2s-%2s-%4s", dia, mes, anio);
                    }
                    printf("Préstamo realizado del libro: ISBN %d, Ejemplar %d\n", op->isbn, libros[i].ejemplares[j].numero);
                    char respuesta[256];
                    snprintf(respuesta, sizeof(respuesta), "Préstamo exitoso: ISBN %d, Ejemplar %d", op->isbn, libros[i].ejemplares[j].numero);
                    enviarRespuesta(op->pid, respuesta);
                    encontrado = 1;
                    return;
                }
            }
            if (!encontrado) {
                char respuesta[256];
                snprintf(respuesta, sizeof(respuesta), "Error: No se encontró un ejemplar disponible para ISBN %d", op->isbn);
                enviarRespuesta(op->pid, respuesta);
                printf("No se encontró un ejemplar disponible para ISBN %d\n", op->isbn);
            }
            return;
        }
    }
    if (!libroEncontrado) {
        char respuesta[256];
        snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op->isbn);
        enviarRespuesta(op->pid, respuesta);
        printf("ISBN %d no encontrado\n", op->isbn);
    }
}

// Guarda el estado final de la base de datos en un archivo de salida
void guardarSalida(char *fileSalida, struct Libros *libros, int numLibros) {
    FILE *salida = fopen(fileSalida, "w");
    if (!salida) {
        printf("Error al crear el archivo de salida\n");
        return;
    }
    for (int i = 0; i < numLibros; i++) {
        fprintf(salida, "%s,%d,%d\n", libros[i].nombre, libros[i].isbn, libros[i].numEj);
        for (int j = 0; j < libros[i].numEj; j++) {
            fprintf(salida, "%d,%c,%s\n", libros[i].ejemplares[j].numero, libros[i].ejemplares[j].status, libros[i].ejemplares[j].fecha);
        }
    }
    fclose(salida);
}

// Proceso principal
int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6 && argc != 7 && argc != 8) {
        printf("\n \t\tUse: $./receptor –p pipeReceptor –f filedatos [-v] [–s filesalida]\n");
        exit(1);
    }

    char *pipeRec = NULL;
    char *nomArchivo = NULL;
    int verbose = 0;
    char *fileSalida = NULL;
    struct Libros libros[MAX_LIBROS];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pipeRec = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            nomArchivo = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            fileSalida = argv[++i];
        }
    }

    if (!pipeRec || !nomArchivo) {
        printf("\n \t\tUse: $./receptor –p pipeReceptor –f filedatos [-v] [–s filesalida]\n");
        exit(1);
    }

    if (mkfifo(pipeRec, 0666) == -1 && errno != EEXIST) {
        printf("Error al crear el pipe %s\n", pipeRec);
        exit(1);
    }
    int fd = open(pipeRec, O_RDWR);
    if (fd < 0) {
        printf("Error al abrir el pipe %s\n", pipeRec);
        exit(1);
    }
    int numLibros = leerDB(nomArchivo, libros);
    if (numLibros <= 0 || numLibros > MAX_LIBROS) {
        printf("Error cargando la base de datos\n");
        close(fd);
        unlink(pipeRec);
        exit(1);
    }

    // Paralelismo con OpenMP
    #pragma omp parallel num_threads(3)
    {
        int tid = omp_get_thread_num();
        if (tid == 0) {
            // Hilo para manejar operaciones D y R
            auxiliar1(libros, numLibros);
        } else if (tid == 1) {
            // Hilo para manejar comandos del usuario
            auxiliar2(libros, numLibros);
        } else if (tid == 2) {
            // Hilo principal para leer el pipe
            struct Operaciones op;
            while (!terminar) {
                int resultado = leerPipe(fd, &op, verbose);
                if (resultado == 0 && bufferCont == 0) {
                    #pragma omp critical(terminar_access)
                    {
                        if (!terminar) {
                            terminar = 1;
                        }
                    }
                    break;
                }
                if (resultado == 1) {
                    anadirBuffer(&op);
                } else if (resultado == 2) {
                    prestamoProceso(&op, libros, numLibros);
                }
            }
        }
    }

    close(fd);
    if (fileSalida) {
        guardarSalida(fileSalida, libros, numLibros);
    }
    unlink(pipeRec);
    return 0;
}
