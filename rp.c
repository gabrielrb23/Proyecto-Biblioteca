#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
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

// Estructura para almacenar los datos de la base de datos (archivo)
typedef struct{
	FILE *archivo;	// Apuntador al archivo de base de datos
	char file_name[64]; // Nombre del archivo
	char isbnLibro[100]; // ISBN del libro
} Archivo;

Archivo database; // Instancia de la estructura Archivo

// Declaración de funciones para manejar los libros
int buscarLibro(char*);
void cambiarFecha(int, int);
void reescribirArchivo(const char*, const char*);

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
	
	//Borrar esto cuando se tenga un archivo de salida
	fileSalida = fileSalida ? fileSalida : "salida.txt";

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
	int fd_CS = open(fifo_CS, O_RDONLY);
	if (fd_CS == -1) {
		perror("Error abriendo fifo_CS");
		exit(1);
	}

	// Abre el pipe Servidor-Cliente en modo escritura
	int fd_SC = open(fifo_SC, O_WRONLY | O_NONBLOCK);  // Abre para escribir respuestas al cliente
	if (fd_SC == -1) {
		perror("Error abriendo fifo_SC");
		exit(1);
	}
	
	// Muestra mensaje de bienvenida
	printf("Bienvenido al sistema receptor de solicitudes NSQK\n\n");

	// Inicializa los semáforos
	sem_init(&vacio, 0, N);  // Inicializa semáforo de espacios vacíos en el buffer
	sem_init(&lleno, 0, 0);  // Inicializa semáforo de espacios llenos en el buffer
	sem_init(&mutex, 0, 1);  // Inicializa semáforo de acceso exclusivo a la cola

	pthread_t auxiliar1;  // Hilo para manejar solicitudes
	pthread_create(&auxiliar1, NULL, manejoRequerimientos, NULL);  // Crea un hilo para manejar las solicitudes

	int read_bytes;
	Requerimiento req;  // Solicitud de operación
	
	// Bucle principal que procesa las solicitudes de los clientes
	while(continuar){
		read_bytes = read(fd_CS, &req, sizeof(Requerimiento));  // Lee solicitud del cliente
		if (read_bytes == -1) {
			perror("Error al leer del FIFO");
			close(fd_CS);
			close(fd_SC);
			exit(1);
		}

		// Si la opción verbose está habilitada, imprime la solicitud recibida
		if(verbose){
			printf("Recibido: %c, %s, %s\n", req.operacion, req.nombre, req.isbn);
		}

		// Maneja el caso de salida (operación 'Q')
		if(req.operacion == 'Q'){
			continuar = 0;  // Finaliza el bucle
			sem_post(&lleno);  // Libera un espacio en el buffer
			break;

		// Maneja las operaciones de devolver ('D') o renovar ('R')
		}else if(req.operacion == 'D' || req.operacion == 'R'){
		
			char *msg = (char *)malloc(256 * sizeof(char));  // Crea un mensaje de respuesta
			if(req.operacion == 'D'){
				sprintf(msg, "La biblioteca ha recibido el libro %s\n", req.nombre);
			}else{
				sprintf(msg, "La biblioteca ha renovado la fecha de entrega del libro %s\n", req.nombre);
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
		}else{

			char estadoActual, fecha[12], nueva_fecha_str[12];
			int ejemplar, encontrado = 0;
			char linea[256], nueva_linea[256] = "";

			database.archivo = fopen(database.file_name,"r+");
			if(database.archivo == NULL){
				perror("No se pudo abrir el archivo");
				break;
			}

			int cantidad = buscarLibro(req.isbn);  // Busca el libro en la base de datos
			strcpy(database.isbnLibro, req.isbn);
			
			while(cantidad > 0 && fgets(linea, sizeof(linea), database.archivo) && !encontrado) {
				if(sscanf(linea, "%d, %c, %s\n", &ejemplar, &estadoActual, fecha) == 3) {
					if(estadoActual == 'D') {
						encontrado = 1;
						estadoActual = 'P';

						time_t ahora = time(NULL);
						ahora += 7 * 24 * 60 * 60; // Suma 7 días
						struct tm *hoy = localtime(&ahora);

						strftime(nueva_fecha_str, sizeof(nueva_fecha_str), "%d-%m-%Y", hoy);
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
				sprintf(msg, "El libro %s se encuentra disponible, debe devolverlo el %s\n", req.nombre, nueva_fecha_str);
			}else {
				sprintf(msg, "El libro %s no se encuentra disponible.\n", req.nombre);
			}

			fclose(database.archivo);  // Cierra el archivo de la base de datos

			// Envía la respuesta al cliente
			ssize_t bytes_written = write(fd_SC, msg, strlen(msg) +1);
			if(bytes_written == -1){
				perror("Error escribiendo en el FIFO");
				close(fd_SC);
				close(fd_CS);
				free(msg);
				exit(1);
			}
			free(msg);
		}
		sleep(2);  // Pausa para evitar sobrecargar la CPU
	}

	// Cierra los pipes y espera que el hilo termine
	close(fd_SC);
	close(fd_CS);

	pthread_join(auxiliar1, NULL);  // Espera al hilo que maneja los requerimientos

	// Destruye los semáforos
	sem_destroy(&vacio);
	sem_destroy(&lleno);
	sem_destroy(&mutex);

	return 0;
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
				if(bandera) {
					// Si es renovación, agrega 7 días a la fecha
					int dia, mes, año;
					if(sscanf(fecha, "%d-%d-%d", &dia, &mes, &año) != 3) {
						printf("Formato de fecha inválido\n");
						continue;
					}

					struct tm fecha_tm = {0};
					fecha_tm.tm_mday = dia;
					fecha_tm.tm_mon = mes - 1;
					fecha_tm.tm_year = año - 1900;

					time_t tiempo = mktime(&fecha_tm);
					tiempo += 7 * 24 * 60 * 60; // Suma 7 días
					struct tm *nueva_fecha = localtime(&tiempo);

					strftime(nueva_fecha_str, sizeof(nueva_fecha_str), "%d-%m-%Y", nueva_fecha);
				}else {
					// Si es devolución, establece la fecha actual
					time_t ahora = time(NULL);
					struct tm *hoy = localtime(&ahora);
					strftime(nueva_fecha_str, sizeof(nueva_fecha_str), "%d-%m-%Y", hoy);
					estadoActual = 'D';  // Marca el libro como devuelto
				}

				snprintf(nueva_linea, sizeof(nueva_linea), "%d, %c, %s\n", ejemplar, estadoActual, nueva_fecha_str);
				break;
			}
		}
		cantidad--;
	}

	// Si se encontró el libro, reescribe la línea con la nueva fecha
	if(encontrado) {
		reescribirArchivo(linea, nueva_linea);
	}else {
		printf("No hay libros prestados.\n");
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