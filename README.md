# Sistema de Préstamo de Libros 📚

Este repositorio contiene la implementación de un sistema concurrente para la gestión de préstamos de libros, desarrollado como proyecto final para el curso **Sistemas Operativos (SIU4085)** de la **Pontificia Universidad Javeriana**.

## 📌 Descripción General

El sistema permite a múltiples procesos solicitantes (PS) realizar operaciones de **préstamo**, **renovación**, **devolución** y **terminación** sobre una base de datos de libros, gestionadas de manera concurrente por un proceso central llamado **Receptor de Peticiones (RP)**. Se hace uso de:

- **Hilos POSIX (pthreads)**
- **Tuberías nombradas (FIFOs)**
- **Sincronización con mutex y variables de condición**
- Alternativas de implementación con **OpenMP** y **fork()**

## ⚙️ Estructura del Sistema

### Procesos
- `solicitante`: representa a un usuario que envía solicitudes al RP.
- `receptor`: recibe y procesa las solicitudes. Puede ejecutarse con:
  - `pthreads`: `receptorPOSIX.c`
  - `OpenMP`: `receptorOpenMP.c`
  - `fork`: `receptorFork.c`

### Hilos en RP (versión POSIX)
- **Principal**: procesa solicitudes de préstamo.
- **Auxiliar1**: gestiona renovaciones y devoluciones desde un buffer compartido.
- **Auxiliar2**: maneja comandos por consola (`s`, `x`).

### Comunicación
- **Tubería principal** `pipeReceptor`: PS → RP
- **Tuberías temporales**: RP → PS (respuestas)
- **Buffer compartido** (en POSIX/OpenMP) o `pipeBuffer` (en fork): hilos o procesos se comunican internamente en el RP.

## 📁 Archivos Importantes

- `solicitante.c`: Código del proceso solicitante
- `receptorPOSIX.c`: Receptor con hilos POSIX
- `receptorOpenMP.c`: Receptor con OpenMP
- `receptorFork.c`: Receptor con procesos `fork`
- `archivoDatos.txt`: Base de datos inicial de libros
- `Makefile`: Script de compilación

## ✅ Funcionalidades Implementadas

- ✅ Solicitud de préstamo (P)
- ✅ Renovación (R)
- ✅ Devolución (D)
- ✅ Terminación de sesión (Q)
- ✅ Reporte del estado (`x`)
- ✅ Finalización del sistema (`s`)
- ✅ Comunicación robusta con múltiples procesos concurrentes
- ✅ Manejadores de error y validación de entrada

## 🧪 Plan de Pruebas

Se incluyen pruebas que abarcan:
- Casos válidos (préstamo, renovación, devolución)
- Casos inválidos (ISBN inexistente, sin ejemplares, formato erróneo)
- Condiciones de carrera y concurrencia con múltiples PS
- Finalización ordenada de procesos

## 🧠 Lecciones Aprendidas

- OpenMP simplifica pero es menos flexible que pthreads.
- fork proporciona aislamiento pero introduce sobrecarga.
- La elección de la herramienta de concurrencia debe adaptarse al tipo de tareas y comunicación.

## 🚧 Mejoras Futuras

- Interfaz gráfica (Qt o web)
- Uso de base de datos relacional (ej. SQLite)
- Soporte multi-biblioteca
- Notificaciones automáticas a usuarios

## 👥 Autores

- Carlos Santiago Pinzón  
- Jorge Enrique Olaya  
- Samuel Jerónimo Gantiva  
- Daniel Hoyos  
- Juan Sebastián Álvarez  

**Profesor:** John Corredor  
**Fecha:** 29 de mayo de 2025

---

> Para más detalles técnicos, revisa el informe completo en `Informe_Proyecto.pdf`.