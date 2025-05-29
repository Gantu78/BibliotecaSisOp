/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Grupo Delta (SAMUEL GANTIVA, CARLOS PINZON, SEBASTIAN ALVAREZ, JORGE OLAYA, DANIEL HOYOS)
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: solicitante.c
#	Descripción: Implementación del proceso solicitante que envía operaciones (préstamo, devolución, renovación) 
#                al receptor a través de un pipe nombrado. Puede operar en modo interactivo o leyendo un archivo de operaciones.
#****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include "receptor.h"

// Prototipo de función para manejar señales
void signal_handler(int sig);

// Proceso principal
int main(int argc, char *argv[]) {
    // Manejo de señales para salida limpia
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Verificar argumentos
    if (argc != 3 && argc != 5) {
        printf("\n \t\tUse: $./solicitante [-i fileoperaciones] –p pipeReceptor\n");
        exit(1);
    }

    char *pipeReceptor = NULL;
    char *fileOperaciones = NULL;
    int modoArchivo = 0;

    // Parsear argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            fileOperaciones = argv[++i];
            modoArchivo = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pipeReceptor = argv[++i];
        }
    }

    if (!pipeReceptor) {
        printf("\n \t\tUse: $./solicitante [-i fileoperaciones] –p pipeReceptor\n");
        exit(1);
    }

    // Abrir el pipe principal para enviar operaciones al receptor
    int fd = open(pipeReceptor, O_WRONLY);
    if (fd < 0) {
        printf("Error al abrir el pipe %s: %s\n", pipeReceptor, strerror(errno));
        exit(1);
    }

    // Crear un pipe específico para recibir respuestas del receptor
    char pipeRespuesta[20];
    snprintf(pipeRespuesta, sizeof(pipeRespuesta), "pipe_%d", getpid());
    if (mkfifo(pipeRespuesta, 0666) == -1 && errno != EEXIST) {
        printf("Error al crear el pipe %s: %s\n", pipeRespuesta, strerror(errno));
        close(fd);
        exit(1);
    }

    // Abrir el pipe de respuesta para lectura
    int fd_respuesta = open(pipeRespuesta, O_RDONLY | O_NONBLOCK);
    if (fd_respuesta < 0) {
        printf("Error al abrir el pipe de respuesta %s: %s\n", pipeRespuesta, strerror(errno));
        close(fd);
        unlink(pipeRespuesta);
        exit(1);
    }

    struct Operaciones op;
    op.pid = getpid(); // PID del solicitante

    if (modoArchivo) {
        // Modo archivo: leer operaciones desde fileOperaciones
        FILE *archivo = fopen(fileOperaciones, "r");
        if (!archivo) {
            printf("Error al abrir el archivo %s: %s\n", fileOperaciones, strerror(errno));
            close(fd);
            close(fd_respuesta);
            unlink(pipeRespuesta);
            exit(1);
        }

        char linea[256];
        while (fgets(linea, sizeof(linea), archivo)) {
            linea[strcspn(linea, "\n")] = 0; // Eliminar salto de línea
            if (sscanf(linea, "%c,%249[^,],%d", &op.tipo, op.nombre, &op.isbn) != 3) {
                printf("Formato inválido en línea: %s\n", linea);
                continue;
            }

            // Enviar operación al receptor
            char mensaje[256];
            snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, op.pid);
            if (write(fd, mensaje, strlen(mensaje) + 1) == -1) {
                printf("Error al escribir en el pipe %s: %s\n", pipeReceptor, strerror(errno));
                break;
            }

            // Esperar y leer la respuesta del receptor
            char respuesta[256];
            int bytes_leidos = 0;
            int intentos = 5;
            while (intentos-- > 0) {
                bytes_leidos = read(fd_respuesta, respuesta, sizeof(respuesta) - 1);
                if (bytes_leidos > 0) {
                    respuesta[bytes_leidos] = '\0';
                    printf("Respuesta del receptor: %s\n", respuesta);
                    break;
                }
                usleep(100000); // Esperar 100ms antes de reintentar
            }

            if (bytes_leidos <= 0) {
                printf("No se recibió respuesta del receptor para la operación: %s\n", mensaje);
            }

            if (op.tipo == 'Q') {
                break; // Terminar si la operación es Q
            }
        }

        fclose(archivo);
    } else {
        // Modo interactivo: leer operaciones desde la entrada estándar
        printf("Ingrese operaciones (formato: tipo,nombre,isbn, ej: P,Libro 1,12345)\n");
        printf("Tipos válidos: P (préstamo), D (devolución), R (renovación), Q (salir)\n");

        char entrada[256];
        while (1) {
            printf("Operación: ");
            if (fgets(entrada, sizeof(entrada), stdin) == NULL) {
                printf("Error al leer la entrada\n");
                continue;
            }

            entrada[strcspn(entrada, "\n")] = 0; // Eliminar salto de línea
            if (sscanf(entrada, "%c,%249[^,],%d", &op.tipo, op.nombre, &op.isbn) != 3) {
                printf("Formato inválido. Use: tipo,nombre,isbn\n");
                continue;
            }

            // Enviar operación al receptor
            char mensaje[256];
            snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, op.pid);
            if (write(fd, mensaje, strlen(mensaje) + 1) == -1) {
                printf("Error al escribir en el pipe %s: %s\n", pipeReceptor, strerror(errno));
                break;
            }

            // Esperar y leer la respuesta del receptor
            char respuesta[256];
            int bytes_leidos = 0;
            int intentos = 5;
            while (intentos-- > 0) {
                bytes_leidos = read(fd_respuesta, respuesta, sizeof(respuesta) - 1);
                if (bytes_leidos > 0) {
                    respuesta[bytes_leidos] = '\0';
                    printf("Respuesta del receptor: %s\n", respuesta);
                    break;
                }
                usleep(100000); // Esperar 100ms antes de reintentar
            }

            if (bytes_leidos <= 0) {
                printf("No se recibió respuesta del receptor para la operación: %s\n", mensaje);
            }

            if (op.tipo == 'Q') {
                break; // Terminar si la operación es Q
            }
        }
    }

    // Enviar operación de terminación si no se envió antes
    op.tipo = 'Q';
    strcpy(op.nombre, "Terminar");
    op.isbn = 0;
    char mensaje[256];
    snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, op.pid);
    write(fd, mensaje, strlen(mensaje) + 1);

    // Cerrar pipes y eliminar pipe de respuesta
    close(fd);
    close(fd_respuesta);
    unlink(pipeRespuesta);

    return 0;
}

// Manejador de señales para salida limpia
void signal_handler(int sig) {
    printf("Señal recibida (%d), terminando solicitante...\n", sig);
    char pipeRespuesta[20];
    snprintf(pipeRespuesta, sizeof(pipeRespuesta), "pipe_%d", getpid());
    unlink(pipeRespuesta);
    exit(0);
}
