/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Grupo Delta (SAMUEL GANTIVA, CARLOS PINZON, SEBASTIAN ALVAREZ, JORGE OLAYA, DANIEL HOYOS)
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: receptor.c
#	Descripcion: Implementación del proceso receptor que gestiona la base de datos de libros y 
#                procesa operaciones (préstamo, devolución, renovación) enviadas por el solicitante. 
#                Utiliza fork y named pipes para comunicación y sincronización.
#****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include "receptor.h"

// Variable global para manejar la terminación
volatile sig_atomic_t terminar = 0;
pid_t pid_auxiliar1 = 0; // PID del proceso hijo que reemplaza a auxiliar1
pid_t pid_auxiliar2 = 0; // PID del proceso hijo que reemplaza a auxiliar2

// Manejador de señales para SIGTERM
void manejar_sigterm(int sig) {
    terminar = 1;
}

// Función que lee la base de datos de libros desde un archivo de texto y la carga en memoria
int leerDB(char *nomArchivo, struct Libros *libros) {
    FILE *archivo = fopen(nomArchivo, "r");
    if (!archivo) {
        return -1;
    }

    char linea[256];
    int cont = 0;
    while (fgets(linea, sizeof(linea), archivo) && cont < MAX_LIBROS) {
        if (linea[0] == '\n' || linea[0] == '\0') continue;
        linea[strcspn(linea, "\n")] = 0;
        if (sscanf(linea, "%249[^,],%d,%d", libros[cont].nombre, &libros[cont].isbn, &libros[cont].numEj) == 3) {
            if (libros[cont].numEj <= 0 || libros[cont].numEj > MAX_EJEMPLAR) {
                continue;
            }
            for (int i = 0; i < libros[cont].numEj && fgets(linea, sizeof(linea), archivo); i++) {
                linea[strcspn(linea, "\n")] = 0;
                struct Ejemplar *e = &libros[cont].ejemplares[i];
                char fecha_str[11];
                if (sscanf(linea, "%d%*c%*c%c%*c%10[^,]", &e->numero, &e->status, fecha_str) == 3) {
                    int dia, mes, anio;
                    if (sscanf(fecha_str, "%d-%d-%d", &dia, &mes, &anio) == 3) {
                        snprintf(e->fecha, sizeof(e->fecha), "%02d-%02d-%04d", dia, mes, anio);
                    } else {
                        snprintf(e->fecha, sizeof(e->fecha), "01-01-2000");
                    }
                } else {
                    continue;
                }
            }
            cont++;
        }
    }
    fclose(archivo);
    return cont;
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
        return;
    }

    write(fd, mensaje, strlen(mensaje) + 1);
    close(fd);
}

// Lee una operación enviada por el solicitante a través del pipe principal
int leerPipe(int fd, struct Operaciones *op, int verbose) {
    char buffer[256];
    int bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No hay datos disponibles, continuar esperando
        }
        return 0; // Error o fin del pipe
    } 

    buffer[bytes] = '\0';

    if (sscanf(buffer, "%c,%249[^,],%d,%d", &op->tipo, op->nombre, &op->isbn, &op->pid) != 4) {
        return 0;
    }

    if (op->tipo == 'Q') {
        terminar = 1;
        return 0;
    } else if (op->tipo == 'D' || op->tipo == 'R') {
        return 1;
    } else if (op->tipo == 'P') {
        return 2;
    }

    return 0;
}

// Procesa las operaciones de devolución y renovación
void procesarDR(struct Libros *libros, int numLibros, int fd_read) {
    struct Operaciones op;
    char buffer[256];

    while (1) {
        int bytes = read(fd_read, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            if (terminar) break;
            usleep(100000);
            continue;
        }

        buffer[bytes] = '\0';
        if (sscanf(buffer, "%c,%249[^,],%d,%d", &op.tipo, op.nombre, &op.isbn, &op.pid) != 4) {
            continue;
        }

        if (op.tipo == 'Q') {
            break;
        }

        for (int i = 0; i < numLibros; i++) {
            if (libros[i].isbn == op.isbn && strcmp(libros[i].nombre, op.nombre) == 0) {
                int encontrado = 0;
                for (int j = 0; j < libros[i].numEj; j++) {
                    if (libros[i].ejemplares[j].status == 'P' && !encontrado) {
                        if (op.tipo == 'D') {
                            libros[i].ejemplares[j].status = 'D';
                            char respuesta[256];
                            snprintf(respuesta, sizeof(respuesta), "Devolución exitosa: ISBN %d, Ejemplar %d", op.isbn, libros[i].ejemplares[j].numero);
                            enviarRespuesta(op.pid, respuesta);
                            encontrado = 1;
                            break;
                        } else if (op.tipo == 'R') {
                            char dia[3], mes[3], anio[5];
                            sscanf(libros[i].ejemplares[j].fecha, "%2s-%2s-%4s", dia, mes, anio);
                            int d = atoi(dia);
                            d += 7;
                            if (d > 30) {
                                d -= 30;
                                int m = atoi(mes);
                                m++;
                                if (m < 1 || m > 12) {
                                    m = 1;
                                }
                                snprintf(mes, sizeof(mes), "%02d", m);
                            }
                            if (d < 1 || d > 30) d = 1;
                            snprintf(dia, sizeof(dia), "%02d", d);
                            dia[2] = '\0';
                            mes[2] = '\0';
                            anio[4] = '\0';
                            snprintf(libros[i].ejemplares[j].fecha, 11, "%2s-%2s-%4s", dia, mes, anio);
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
                }
                break;
            }
            if (i == numLibros - 1) {
                char respuesta[256];
                snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op.isbn);
                enviarRespuesta(op.pid, respuesta);
            } 
        }
    }
}

// Escribe el estado de libros en un archivo temporal para el reporte
void escribirTempLibros(struct Libros *libros, int numLibros) {
    FILE *temp = fopen("temp_libros.txt", "w");
    if (!temp) {
        return;
    }
    for (int i = 0; i < numLibros; i++) {
        fprintf(temp, "%s,%d,%d\n", libros[i].nombre, libros[i].isbn, libros[i].numEj);
        for (int j = 0; j < libros[i].numEj; j++) {
            fprintf(temp, "%d,%c,%s\n", libros[i].ejemplares[j].numero, libros[i].ejemplares[j].status, libros[i].ejemplares[j].fecha);
        }
    }
    fclose(temp);
}

// Maneja comandos interactivos del usuario (s para salir, r para generar reporte)
void manejarComandos(struct Libros *libros, int numLibros) {
    char comando[3];
    while (1) {
        if (scanf("%2s", comando) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
        if (strcmp(comando, "s") == 0) {
            kill(getppid(), SIGTERM); // Enviar señal al padre
            kill(pid_auxiliar1, SIGTERM); // Enviar señal al otro hijo
            break;
        } else if (strcmp(comando, "r") == 0) {
            kill(getppid(), SIGUSR1);
            usleep(200000); // Esperar a que el padre escriba el archivo

            FILE *temp = fopen("temp_libros.txt", "r");
            if (!temp) {
                continue;
            }

            char linea[256];
            while (fgets(linea, sizeof(linea), temp)) {
                linea[strcspn(linea, "\n")] = 0;
                int isbn, numEj, numero;
                char nombre[250], status, fecha[11];
                if (sscanf(linea, "%249[^,],%d,%d", nombre, &isbn, &numEj) == 3) {
                    for (int j = 0; j < numEj && fgets(linea, sizeof(linea), temp); j++) {
                        linea[strcspn(linea, "\n")] = 0;
                        if (sscanf(linea, "%d,%c,%10s", &numero, &status, fecha) == 3) {
                            printf("%c, %s, %d, %d, %s\n", status, nombre, isbn, numero, fecha);
                            fflush(stdout);
                        }
                    }
                }
            }
            fclose(temp);
        }
    }
}

// Procesa una operación de préstamo, actualizando el estado de un ejemplar disponible.
void prestamoProceso(struct Operaciones *op, struct Libros *libros, int numLibros) {
    int libroEncontrado = 0;
    for (int i = 0; i < numLibros; i++) {
        if (libros[i].isbn == op->isbn && strcmp(libros[i].nombre, op->nombre) == 0) {
            libroEncontrado = 1;
            int encontrado = 0;
            for (int j = 0; j < libros[i].numEj; j++) {
                if (libros[i].ejemplares[j].status == 'D') {
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
            }
            return;
        }
    }
    if (!libroEncontrado) {
        char respuesta[256];
        snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op->isbn);
        enviarRespuesta(op->pid, respuesta);
    }
}

// Guarda el estado final de la base de datos en un archivo de salida
void guardarSalida(char *fileSalida, struct Libros *libros, int numLibros) {
    FILE *salida = fopen(fileSalida, "w");
    if (!salida) {
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

// Manejador de SIGUSR1 para escribir el estado de libros
void manejar_sigusr1(int sig) {
    extern struct Libros *libros_global;
    extern int numLibros_global;
    escribirTempLibros(libros_global, numLibros_global);
}

// Proceso principal. Inicializa los recursos, crea procesos hijos, y procesa operaciones
struct Libros *libros_global = NULL; // Para acceso en manejador de señales
int numLibros_global = 0;

int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6 && argc != 7 && argc != 8) {
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
        exit(1);
    }

    // Configurar manejadores de señales
    signal(SIGTERM, manejar_sigterm);
    signal(SIGUSR1, manejar_sigusr1);

    // Crear pipe principal
    if (mkfifo(pipeRec, 0666) == -1 && errno != EEXIST) {
        exit(1);
    }

    int fd = open(pipeRec, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        unlink(pipeRec);
        exit(1);
    }

    // Crear pipe para comunicación con el hijo que procesa D/R (reemplazo de auxiliar1)
    const char *pipeBuffer = "pipeBuffer";
    if (mkfifo(pipeBuffer, 0666) == -1 && errno != EEXIST) {
        close(fd);
        unlink(pipeRec);
        exit(1);
    }

    // Leer la base de datos
    int numLibros = leerDB(nomArchivo, libros);
    if (numLibros <= 0 || numLibros > MAX_LIBROS) {
        close(fd);
        unlink(pipeRec);
        unlink(pipeBuffer);
        exit(1);
    }

    // Guardar variables globales para el manejador de señales
    libros_global = libros;
    numLibros_global = numLibros;

    // Crear proceso hijo para procesar D/R (reemplazo de auxiliar1) después de leerDB
    fprintf(stderr, "Creando proceso hijo para D/R...\n");
    pid_auxiliar1 = fork();
    if (pid_auxiliar1 < 0) {
        close(fd);
        unlink(pipeRec);
        unlink(pipeBuffer);
        exit(1);
    } else if (pid_auxiliar1 == 0) {
        // Proceso hijo: reemplazo de auxiliar1
        fprintf(stderr, "Proceso hijo D/R iniciado (PID = %d)\n", getpid());
        int fd_read_buffer = open(pipeBuffer, O_RDONLY);
        if (fd_read_buffer < 0) {
            exit(1);
        }
        signal(SIGTERM, manejar_sigterm);
        procesarDR(libros, numLibros, fd_read_buffer);
        close(fd_read_buffer);
        fprintf(stderr, "Proceso hijo D/R terminado (PID = %d)\n", getpid());
        exit(0);
    }

    // Abrir pipeBuffer para escritura
    int fd_write_buffer = -1;
    int intentos = 10; // Límite de reintentos
    while (fd_write_buffer < 0 && intentos > 0) {
        fd_write_buffer = open(pipeBuffer, O_WRONLY | O_NONBLOCK);
        if (fd_write_buffer < 0) {
            if (errno == ENXIO) {
                usleep(100000); // Esperar 100ms antes de reintentar
                intentos--;
            } else {
                kill(pid_auxiliar1, SIGTERM);
                close(fd);
                unlink(pipeRec);
                unlink(pipeBuffer);
                exit(1);
            }
        }
    }
    if (fd_write_buffer < 0) {
        kill(pid_auxiliar1, SIGTERM);
        close(fd);
        unlink(pipeRec);
        unlink(pipeBuffer);
        exit(1);
    }

    // Crear proceso hijo para manejar comandos interactivos (reemplazo de auxiliar2)
    fprintf(stderr, "Creando proceso hijo para comandos...\n");
    pid_auxiliar2 = fork();
    if (pid_auxiliar2 < 0) {
        kill(pid_auxiliar1, SIGTERM);
        close(fd);
        close(fd_write_buffer);
        unlink(pipeRec);
        unlink(pipeBuffer);
        exit(1);
    } else if (pid_auxiliar2 == 0) {
        // Proceso hijo: reemplazo de auxiliar2
        close(fd); // No necesita leer del pipe principal
        close(fd_write_buffer); // No necesita escribir en pipeBuffer
        fprintf(stderr, "Proceso hijo comandos iniciado (PID = %d)\n", getpid());
        signal(SIGTERM, manejar_sigterm);
        manejarComandos(libros, numLibros);
        fprintf(stderr, "Proceso hijo comandos terminado (PID = %d)\n", getpid());
        exit(0);
    }

    // Proceso padre: lee operaciones del pipe principal y distribuye
    struct Operaciones op;
    while (!terminar) {
        int resultado = leerPipe(fd, &op, verbose);
        if (resultado == 0) {
            if (terminar) {
                char mensaje[256];
                snprintf(mensaje, sizeof(mensaje), "Q,Salir,0,0");
                write(fd_write_buffer, mensaje, strlen(mensaje) + 1);
                break;
            }
            usleep(100000);
            continue;
        }
        if (resultado == 1) { // Operaciones D o R
            char mensaje[256];
            snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, op.pid);
            write(fd_write_buffer, mensaje, strlen(mensaje) + 1);
        } else if (resultado == 2) { // Operación P
            prestamoProceso(&op, libros, numLibros);
        }
    }

    // Esperar a los procesos hijos
    waitpid(pid_auxiliar1, NULL, 0);
    waitpid(pid_auxiliar2, NULL, 0);

    // Cerrar recursos
    close(fd);
    close(fd_write_buffer);
    if (fileSalida) {
        guardarSalida(fileSalida, libros, numLibros);
    }
    unlink(pipeRec);
    unlink(pipeBuffer);
    unlink("temp_libros.txt");
    return 0;
}
