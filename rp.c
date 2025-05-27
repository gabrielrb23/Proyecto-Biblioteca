/**************************************************************
*	Pontificia Universidad Javeriana
*	Autor: Gabriel Riaño y Dary Palacios
*	Materia: Sistemas Operativos
*	Fecha: 26/5/2025
*	Descripción: Este programa implementa el servidor del sistema
*   de préstamos de libros de la biblioteca. Gestiona solicitudes
*   de préstamo, devolución, renovación y salida  recibidas a través
*   de pipes FIFO. Utiliza un buffer circular con semáforos para
*   procesar solicitudes de forma concurrente, actualiza la base
*   de datos de libros al cambiar estados y fechas, y soporta comandos
*   administrativos como generación de reportes ('r') o terminación ('s').
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>

#define N 10 // Tamaño del buffer circular

// Estructura para almacenar la solicitud de operación
typedef struct{
	char operacion;   // Tipo de operación ('D' para devolver, 'R' para renovar, 'P' para pedir, 'Q' para salir)
	char nombre[30];  // Nombre del libro
	char isbn[30];	// ISBN del libro
} Requerimiento;

// Buffer circular para almacenar las solicitudes
Requerimiento buffer[N];
int in, out; // Índices para insertar y extraer de la cola
int continuar = 1; // Variable de control para continuar la ejecución del servidor

// Semáforos para sincronización de la cola
sem_t vacio, lleno, mutex;

// Función que maneja las solicitudes en el servidor
void* manejoRequerimientos(void*);
// Función que maneja los comandos en la consola
void* manejoComandos(void*);

// Estructura para almacenar los datos de la base de datos (archivo)
typedef struct{
	FILE *archivo;	// Apuntador al archivo de base de datos
	char file_name[64]; // Nombre del archivo
	char isbnLibro[100]; // ISBN del libro
	char nombreLibro[100]; // Nombre del libro
} Archivo;

Archivo database; // Instancia de la estructura Archivo

int buscarLibro(char*);
void cambiarFecha(int, int);
void reescribirArchivo(const char*, const char*);
void generarReporte();
char* obtenerFechaFutura();
void escribirEstadoBD(const char *fileSalida);
void gestionarPrestamo(Requerimiento req, int fd_SC);

int main(int argc, char *argv[]){

	// Verifica que el número de argumentos sea suficiente
	if(argc < 4){
		printf("Uso correcto: $ ./ejecutable -p pipeReceptor –f filedatos [-v] [–s filesalida]\nDonde el contenido de los corchetes es opcional\n");
		return -1;
	}

	int opt;
	char *pipeReceptor = NULL;
	char *fileDatos = NULL;
	char *fileSalida = NULL;
	int verbose = 0; // Bandera para habilitar/deshabilitar mensajes detallados

	// Procesa los parámetros de línea de comandos
	while ((opt = getopt(argc, argv, "p:f:vs:")) != -1) {
		switch (opt) {
			case 'p':
				pipeReceptor = optarg;  // Nombre del pipe receptor
				break;
			case 'f':
				fileDatos = optarg;  // Archivo de datos (base de datos)
				break;
			case 'v':
				verbose = 1;  // Habilita la opción de verbose
				break;
			case 's':
				fileSalida = optarg;  // Archivo de salida (opcional)
				break;
			default:
				fprintf(stderr, "Uso: %s -p pipeReceptor -f filedatos [-v] [-s filesalida]\n", argv[0]);
				exit(1);
		}
	}
	// Verifica que los parámetros obligatorios estén presentes
	if (pipeReceptor == NULL || fileDatos == NULL) {
		fprintf(stderr, "Error: Los parametros -p y -f son obligatorias.\n");
		exit(1);
	}

	strcpy(database.file_name, fileDatos); // Copia el nombre del archivo de base de datos

	// Definición de los nombres de los pipes FIFO para la comunicación cliente-servidor
	char fifo_SC[50], fifo_CS[50];
	snprintf(fifo_CS, sizeof(fifo_CS), "/tmp/%s_CS", pipeReceptor);  // Pipe Cliente-Servidor
	snprintf(fifo_SC, sizeof(fifo_SC), "/tmp/%s_SC", pipeReceptor);  // Pipe Servidor-Cliente
	
	// Crea los pipes FIFO con permisos adecuados
	mkfifo(fifo_CS, S_IFIFO|0640);
	mkfifo(fifo_SC, S_IFIFO|0640);

	// Abre el pipe Cliente-Servidor en modo lectura
	int fd_CS = open(fifo_CS, O_RDONLY | O_NONBLOCK);
	if (fd_CS == -1) {
		perror("Error abriendo fifo_CS");
		exit(1);
	}

	// Abre el pipe Servidor-Cliente en modo lectura/escritura
	int fd_SC = open(fifo_SC, O_RDWR | O_NONBLOCK);  // Abre para lectura/escritura
	if (fd_SC == -1) {
		perror("Error abriendo fifo_SC");
		exit(1);
	}
	
	// Muestra mensaje de bienvenida
	printf("Bienvenido al sistema receptor de solicitudes de la Javeriana\n\n");

	// Inicializa los semáforos
	sem_init(&vacio, 0, N);  // Inicializa semáforo de espacios vacíos en el buffer
	sem_init(&lleno, 0, 0);  // Inicializa semáforo de espacios llenos en el buffer
	sem_init(&mutex, 0, 1);  // Inicializa semáforo de acceso exclusivo a la cola

	pthread_t auxiliar1;  // Hilo para manejar solicitudes
	pthread_t auxiliar2;  // Hilo para manejar comandos de consola
	pthread_create(&auxiliar1, NULL, manejoRequerimientos, NULL);  // Crea un hilo para manejar las solicitudes
	pthread_create(&auxiliar2, NULL, manejoComandos, NULL);  // Crea un hilo para manejar los comandos

	int read_bytes;
	Requerimiento req;  // Solicitud de operación
	
	// Bucle principal que procesa las solicitudes de los clientes
	while(continuar){
		read_bytes = read(fd_CS, &req, sizeof(Requerimiento));
		if (read_bytes == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// No hay datos disponibles, continuar sin bloquear
				usleep(100000);
				continue;
			} else {
				perror("Error al leer del FIFO");
				close(fd_CS);
				close(fd_SC);
				exit(1);
			}
		}

		// Si la opción verbose está habilitada, imprime la solicitud recibida
		if (verbose && read_bytes == sizeof(Requerimiento)) {
			printf("\nRecibido: %c, %s, %s\n", req.operacion, req.nombre, req.isbn);
		}

		// Maneja las operaciones de devolver ('D') o renovar ('R')
		if(req.operacion == 'D' || req.operacion == 'R'){
		
			char *msg = (char *)malloc(256 * sizeof(char));  // Crea un mensaje de respuesta
			if(req.operacion == 'D'){
				sprintf(msg, "La biblioteca esta recibiendo el libro %s\n", req.nombre);
			}else{
				char* nueva_fecha_str = obtenerFechaFutura();  // Obtiene la nueva fecha de entrega
				sprintf(msg, "La biblioteca ha renovado la fecha de entrega del libro %s, entreguelo antes del %s\n", req.nombre, nueva_fecha_str);
			}

			// Envía la respuesta al cliente
			ssize_t bytes_written = write(fd_SC, msg, strlen(msg));
			if(bytes_written == -1){
				perror("Error escribiendo en el FIFO");
				close(fd_SC);
				close(fd_CS);
				free(msg);
				exit(1);
			}
			free(msg);

			// Sincroniza el acceso a la cola de solicitudes con semáforos
			sem_wait(&vacio);
			sem_wait(&mutex);

			// Inserta la solicitud en el buffer circular
			buffer[in] = req;
			in = (in+1)%N;

			sem_post(&mutex);
			sem_post(&lleno);

		// Maneja las solicitudes de préstamo (operación 'P')
		}else if(req.operacion == 'P'){
			gestionarPrestamo(req, fd_SC);
		}else if(req.operacion == 'Q'){ // Maneja el caso de salida (operación 'Q')
			printf("\nEl usuario del PS notifica que no se enviaran mas solicitudes.\n\n");
			break;
		}
		sleep(1);  // Pausa para evitar sobrecargar la CPU
	}

	// Cierra los pipes y espera que el hilo termine
	close(fd_SC);
	close(fd_CS);

	pthread_join(auxiliar1, NULL);  // Espera al hilo que maneja los requerimientos
	pthread_join(auxiliar2, NULL);  // Espera al hilo que maneja los comandos de consola

	// Destruye los semáforos
	sem_destroy(&vacio);
	sem_destroy(&lleno);
	sem_destroy(&mutex);

	// Escribe el estado final de la base de datos en el archivo de salida
	if(fileSalida != NULL){
		escribirEstadoBD(fileSalida);
	}
	return 0;
}

//Funcion que maneja los comandos ingresados en consola
void* manejoComandos(void* arg){

	char buffer[256];
	while(continuar){
		// Lee el comando del usuario
		if(fgets(buffer, sizeof(buffer), stdin) == NULL){
			perror("Error al leer mensaje");
			continue;
		}
		// Elimina el salto de línea al final de la entrada
		if (buffer[strlen(buffer)-1] == '\n') {
			buffer[strlen(buffer)-1] = '\0';
		}

		// Si el comando es "s", termina el programa
		if(strcmp(buffer, "s") == 0){
			continuar = 0;  // Finaliza el bucle
			sem_post(&lleno);  // Actualiza el otro hilo para terminar su ejecucion
			break;
		} else if(strcmp(buffer, "r") == 0){	// Si el comando es "r", genera un reporte
			generarReporte();
			continue;
		}
	}
	return NULL;
}

// Función que maneja las solicitudes de libros
void* manejoRequerimientos(void* arg){
	while(continuar){
		// Espera a que haya una solicitud en el buffer
		sem_wait(&lleno);
		sem_wait(&mutex);

		// Extrae la solicitud del buffer circular
		Requerimiento req = buffer[out];
		out = (out+1) % N;

		sem_post(&mutex);
		sem_post(&vacio);

		// Si el sistema sigue activo, procesa la solicitud
		if(continuar != 0){
			// Abre el archivo de la base de datos en modo lectura y escritura
			database.archivo = fopen(database.file_name,"r+");
			if(database.archivo == NULL){
				perror("No se pudo abrir el archivo");
				break;
			}
			// Busca el libro por su ISBN
			int cantidad = buscarLibro(req.isbn);
			strcpy(database.isbnLibro, req.isbn);
			
			// Cambia la fecha dependiendo de la operación
			if(req.operacion == 'D'){
				cambiarFecha(cantidad, 0);  // Devolver libro
			}else{
				cambiarFecha(cantidad, 1);  // Renovar libro
			}
			fclose(database.archivo);  // Cierra el archivo de la base de datos
		}
		sleep(1);  // Pausa antes de procesar otra solicitud
	}
	return NULL;
}

// Función que genera un reporte de los ejemplares
void generarReporte() {
	// Abre el archivo de base de datos en modo lectura
	FILE *archivo = fopen(database.file_name, "r");
	if (archivo == NULL) {
		perror("No se pudo abrir el archivo de base de datos");
		return;
	}

	char linea[256];
	char nombre[30], isbn[30], estado;
	int ejemplar;
	char fecha[12];

	printf("\nReporte de ejemplares:\n");
	printf("Status, Nombre del Libro, ISBN, Ejemplar, Fecha\n");

	while (fgets(linea, sizeof(linea), archivo)) {
		if (sscanf(linea, "%d, %c, %s\n", &ejemplar, &estado, fecha) == 3) {
			printf("%c, %s, %s, %d, %s\n", estado, database.nombreLibro, isbn, ejemplar, fecha);
		} else if (sscanf(linea, "%29[^,], %29[^,], %d\n", nombre, isbn, &ejemplar) == 3) {
			// Actualiza el nombre e ISBN del libro actual
			strcpy(database.nombreLibro, nombre);
			strcpy(database.isbnLibro, isbn);
		}
	}

	fclose(archivo);
}

// Función que escribe el estado de la base de datos en un archivo
void escribirEstadoBD(const char *fileSalida) {
	// Abre el archivo de base de datos en modo lectura
	FILE *archivo = fopen(database.file_name, "r");
	if (archivo == NULL) {
		perror("No se pudo abrir el archivo de base de datos");
		return;
	}

	// Abre el archivo de salida en modo escritura
	FILE *salida = fopen(fileSalida, "w");
	if (salida == NULL) {
		perror("No se pudo abrir el archivo de salida");
		fclose(archivo);
		return;
	}

	char linea[256];
	char nombre[30], isbn[30], estado;
	int ejemplar, ejemplares = 0, total_disponibles = 0;
	char fecha[12];
	char libro_actual[30] = "";

	fprintf(salida, "Nombre del Libro, ISBN, Ejemplar, Estado, Fecha\n\n");

	while (fgets(linea, sizeof(linea), archivo)) {
		if (sscanf(linea, "%d, %c, %s\n", &ejemplar, &estado, fecha) == 3) {
			if (strcmp(libro_actual, database.nombreLibro) != 0) {
				if (strlen(libro_actual) > 0) {
					fprintf(salida, "Total disponibles: %d\n\n", total_disponibles);
				}
				strcpy(libro_actual, database.nombreLibro);
				total_disponibles = 0; // Reinicia el contador de disponibles
			}
			if (estado == 'D') {
				total_disponibles++;
			}
			// Escribe la información del ejemplar en el archivo de salida
			fprintf(salida, "%s, %s, %d, %c, %s\n", database.nombreLibro, database.isbnLibro, ejemplar, estado, fecha);
		} else if (sscanf(linea, "%29[^,], %29[^,], %d\n", nombre, isbn, &ejemplares) == 3) {
			if (strcmp(libro_actual, nombre) != 0) {
				if (strlen(libro_actual) > 0) {
					fprintf(salida, "Total disponibles: %d\n\n", total_disponibles);
				}
				strcpy(libro_actual, nombre);
				total_disponibles = 0; // Reinicia el contador de disponibles
			}
			strcpy(database.nombreLibro, nombre);
			strcpy(database.isbnLibro, isbn);
			fprintf(salida, "%s, %s, %d: \n", nombre, isbn, ejemplares);
		}
	}

	if (strlen(libro_actual) > 0) {
		fprintf(salida, "Total disponibles: %d\n\n", total_disponibles);
	}

	fclose(archivo);
	fclose(salida);
}

// Función que busca un libro por su ISBN en el archivo
int buscarLibro(char *isbnDado){
	char nombre[30], isbn[30], linea[256];
	int encontrado=0, ejemplares = 0;

	// Lee las líneas del archivo hasta encontrar el libro
	while(fscanf(database.archivo, "%29[^,], %29[^,], %d\n", nombre, isbn, &ejemplares) == 3){
		if(strcmp(isbnDado, isbn) == 0){
			encontrado = 1;
			break;
		}else{
			while(ejemplares>0 && fgets(linea, sizeof(linea), database.archivo)){
				ejemplares--;
			}
		}
	}
	if(!encontrado){
		printf("Libro no encontrado\n");
	}
	return ejemplares;
}

// Función que obtiene la fecha futura (7 días a partir de hoy)
char* obtenerFechaFutura() {
	time_t ahora = time(NULL);
	ahora += 7 * 24 * 60 * 60; // Suma 7 días
	struct tm *hoy = localtime(&ahora);
	static char nueva_fecha_str[12];
	strftime(nueva_fecha_str, sizeof(nueva_fecha_str), "%d-%m-%Y", hoy);
	return nueva_fecha_str;
}

// Función que cambia la fecha de devolución de un libro
void cambiarFecha(int cantidad, int bandera) {
	char estadoActual, fecha[12], nueva_fecha_str[12];
	int ejemplar, encontrado = 0;
	char linea[256];
	char nueva_linea[256] = "";

	// Modifica la fecha dependiendo de si es una renovación o devolución
	while(cantidad > 0 && fgets(linea, sizeof(linea), database.archivo) && !encontrado) {
		if(sscanf(linea, "%d, %c, %s\n", &ejemplar, &estadoActual, fecha) == 3) {
			if(estadoActual == 'P') {
				encontrado = 1;
				time_t ahora = time(NULL);

				if(bandera) {	// Si es renovación, suma 7 días a la fecha actual
					ahora += 7 * 24 * 60 * 60;
				}else {		// Si es devolución, establece la fecha actual
					estadoActual = 'D';
				}
				
				struct tm *hoy = localtime(&ahora);
				strftime(nueva_fecha_str, sizeof(nueva_fecha_str), "%d-%m-%Y", hoy);
				snprintf(nueva_linea, sizeof(nueva_linea), "%d, %c, %s\n", ejemplar, estadoActual, nueva_fecha_str);
				break;
			}
		}
		cantidad--;
	}

	// Si se encontró el libro, reescribe la línea con la nueva fecha
	if(encontrado) {
		reescribirArchivo(linea, nueva_linea);
	}
}

// Función para reescribir el archivo de base de datos con la nueva información
void reescribirArchivo(const char *linea_original, const char *linea_nueva) {
	FILE *temp = fopen("temp.txt", "w");  // Archivo temporal para escritura
	if (!temp) {
		perror("No se pudo abrir archivo temporal");
		return;
	}

	rewind(database.archivo);  // Vuelve al principio del archivo original

	char linea[256];
	char nombre[30], isbn[30], estadoActual, fecha[12];
	int encontrado = 0, cambiado = 0, ejemplares, ejemplar;
	// Recorre el archivo original y escribe las líneas en el archivo temporal
	while(fgets(linea, sizeof(linea), database.archivo)){		
		if (sscanf(linea, "%d, %c, %s\n", &ejemplar, &estadoActual, fecha) == 3){
			if(strcmp(linea, linea_original) == 0 && encontrado && !cambiado) {
				fputs(linea_nueva, temp);  // Escribe la línea con los cambios
				cambiado = 1;
			}else{
				fputs(linea, temp);  // Escribe la línea original
			}
		}else if(sscanf(linea, "%29[^,], %29[^,], %d\n", nombre, isbn, &ejemplares) == 3){
			fputs(linea, temp);  // Escribe la información del libro
			if(strcmp(isbn, database.isbnLibro) == 0){
				encontrado = 1;
			}else{
				encontrado = 0;
			}
		} 
	}

	fclose(temp);  // Cierra el archivo temporal

	remove(database.file_name);  // Elimina el archivo original
	rename("temp.txt", database.file_name);  // Renombra el archivo temporal al nombre original

	database.archivo = fopen(database.file_name, "r+");  // Reabre el archivo original para su edición
	if(database.archivo == NULL) {
		perror("No se pudo abrir el archivo");
		return;
	}
}

// Implementación de la nueva función para gestionar requerimientos 'P'
void gestionarPrestamo(Requerimiento req, int fd_SC) {
    char estadoActual, fecha[12];
    char *nueva_fecha_str = obtenerFechaFutura();
    int ejemplar, encontrado = 0;
    char linea[256], nueva_linea[256] = "";

    database.archivo = fopen(database.file_name,"r+");
    if(database.archivo == NULL){
        perror("No se pudo abrir el archivo");
        return;
    }

    int cantidad = buscarLibro(req.isbn);
    strcpy(database.isbnLibro, req.isbn);

    while(cantidad > 0 && fgets(linea, sizeof(linea), database.archivo) && !encontrado) {
        if(sscanf(linea, "%d, %c, %s\n", &ejemplar, &estadoActual, fecha) == 3) {
            if(estadoActual == 'D') {
                encontrado = 1;
                estadoActual = 'P';
                snprintf(nueva_linea, sizeof(nueva_linea), "%d, %c, %s\n", ejemplar, estadoActual, nueva_fecha_str);
                break;
            }
        }
        cantidad--;
    }

    char *msg = (char *)malloc(256 * sizeof(char));
    // Si se encontró el libro, reescribe la línea con la nueva fecha
    if(encontrado) {
        reescribirArchivo(linea, nueva_linea);
        // Responde al cliente indicando que el libro está disponible
        sprintf(msg, "El libro %s se encuentra disponible, debe devolverlo antes del %s\n", req.nombre, nueva_fecha_str);
    }else {
        sprintf(msg, "El libro %s no se encuentra disponible.\n", req.nombre);
    }

    fclose(database.archivo);  // Cierra el archivo de la base de datos

    // Envía la respuesta al PS
    ssize_t bytes_written = write(fd_SC, msg, strlen(msg) + 1);
    if(bytes_written == -1){
        perror("Error escribiendo en el FIFO");
        close(fd_SC);
        free(msg);
        exit(1);
    }
    free(msg);
}