#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Estructura para almacenar la solicitud de operación
typedef struct{
	char operacion;   // Tipo de operación ('D' para devolver, 'R' para renovar, 'P' para pedir, 'Q' para salir)
	char nombre[30];  // Nombre del libro
	char isbn[30];	// ISBN del libro
} Requerimiento;

void mostrarMenu();
void enviarRequerimiento(int fd_CS, int fd_SC, char operacion, const char *nombre, const char *isbn);
void leerArchivo(const char *fileDatos, int fd_CS, int fd_SC);
int manejarOtraOpcion(int fd_CS, int fd_SC);

int main(int argc, char *argv[]){

	// Verifica que el número de argumentos sea correcto
	if(argc < 3){
		printf("Uso correcto: $ ./ejecutable [-i file] -p pipeReceptor\nDonde el contenido de los corchetes es opcional\n");
		return -1;
	}

	int opt;
	char *pipeReceptor = NULL;
	char *fileDatos = NULL;

	// Analiza los argumentos de la línea de comandos usando getopt
	while ((opt = getopt(argc, argv, "p:i:")) != -1) {
		switch (opt) {
			case 'p':
				pipeReceptor = optarg;  // Se asigna el valor del argumento -p a la variable pipeReceptor
				break;
			case 'i':
				fileDatos = optarg;  // Se asigna el valor del argumento -i a fileDatos
				break;
			default:
				// En caso de un argumento incorrecto, muestra el mensaje de uso correcto y termina el programa
				fprintf(stderr,"Uso correcto: %s [-i file] -p pipeReceptor\nDonde el contenido de los corchetes es opcional\n", argv[0]);
				exit(1);
		}
	}

	// Verifica que el parámetro -p haya sido proporcionado
	if (pipeReceptor == NULL){
		fprintf(stderr, "Error: El parametro -p es obligatorio.\n");
		exit(1);
	}

	// Variables para las rutas de los pipes FIFO
	char fifo_SC[50], fifo_CS[50];
	snprintf(fifo_CS, sizeof(fifo_CS), "/tmp/%s_CS", pipeReceptor);
	snprintf(fifo_SC, sizeof(fifo_SC), "/tmp/%s_SC", pipeReceptor);

	// Abre el pipe de escritura (Client-Server) para enviar datos
	int fd_CS = open(fifo_CS, O_WRONLY);
	if (fd_CS == -1) {
		perror("Error abriendo fifo_CS");
		exit(1);
	}

	// Abre el pipe de lectura (Server-Client) para recibir datos
	int dummy = open(fifo_SC, O_WRONLY | O_NONBLOCK);  // Se abre solo para verificar si existe
	int fd_SC = open(fifo_SC, O_RDONLY);  // Abre el pipe para lectura
	if (fd_SC == -1) {
		perror("Error abriendo fifo_SC");
		exit(1);
	}
	if (dummy != -1) close(dummy);  // Si dummy se abre correctamente, se cierra

	// Imprime un mensaje de bienvenida
	printf("Bienvenido al sistema de prestamo de libros NSQK\n\n");

	// Si se proporciona el archivo de datos, se abre y se procesa
	if (fileDatos != NULL) {
		leerArchivo(fileDatos, fd_CS, fd_SC);
	}

	char buffer[256];

	while(1){

		// Menú para que el usuario seleccione la operación que desea realizar
		mostrarMenu();

		// Lee la opción seleccionada por el usuario
		if(fgets(buffer, sizeof(buffer), stdin) == NULL){
			perror("Error al leer mensaje");
			continue;
		}
		// Elimina el salto de línea al final de la entrada
		if (buffer[strlen(buffer)-1] == '\n') {
			buffer[strlen(buffer)-1] = '\0';
		}

		// Si la opción no es 0, procesa la solicitud
		if(strcmp(buffer, "0") != 0){
			char op;
			char nombre[30];
			char isbn[30];

			// Determina el tipo de operación según la opción seleccionada
			if(strcmp(buffer, "1") == 0){
				op = 'D';  // Devolver un libro
			}else if(strcmp(buffer, "2") == 0){
				op = 'R';  // Renovar un libro
			}else if(strcmp(buffer, "3") == 0){
				op = 'P';  // Solicitar préstamo de un libro
			}else{
				perror("Entrada invalida");
				continue;
			}

			// Solicita el nombre del libro
			printf("Cual es el nombre del libro?\n");
			if(fgets(buffer, sizeof(buffer), stdin) == NULL){
				perror("Error al leer mensaje");
				continue;
			}
			if (buffer[strlen(buffer)-1] == '\n') {
				buffer[strlen(buffer)-1] = '\0';
			}
			strcpy(nombre, buffer);

			// Solicita el ISBN del libro
			printf("Cual es el ISBN del libro?\n");
			if(fgets(buffer, sizeof(buffer), stdin) == NULL){
				perror("Error al leer mensaje");
				continue;
			}
			if (buffer[strlen(buffer)-1] == '\n') {
				buffer[strlen(buffer)-1] = '\0';
			}
			strcpy(isbn, buffer);

			// Escribe la solicitud en el pipe
			enviarRequerimiento(fd_CS, fd_SC, op, nombre, isbn);
		} else {
			// Si se selecciona "0" para salir, envía una señal de salida (Q)
			Requerimiento req = {'Q', "-", "-"};
			ssize_t bytes_written = write(fd_CS, &req, sizeof(Requerimiento));
			if(bytes_written == -1){
				perror("Error al escribir en el FIFO");
			}
			printf("\nGracias por usar nuestro sistema\n");
			close(fd_CS);
			close(fd_SC);
			break;  // Sale del ciclo principal
		}

		// Verifica si el usuario quiere realizar otra solicitud
		if(!manejarOtraOpcion(fd_CS, fd_SC)){
			break;
		}
	}
	return 0;
}

// Función que muestra el menú de opciones al usuario
void mostrarMenu() {
    printf("Ingrese una opcion para realizar su solicitud:\n\n");
    printf("1. Devolver un libro\n");
    printf("2. Renovar un libro\n");
    printf("3. Solicitar prestamo de un libro\n");
    printf("0. Salir\n\n");
    printf("Opcion: ");
}

// Función que envía la solicitud al servidor y recibe la respuesta
void enviarRequerimiento(int fd_CS, int fd_SC, char operacion, const char *nombre, const char *isbn) {
    Requerimiento req = {operacion};
    strcpy(req.nombre, nombre);
    strcpy(req.isbn, isbn);

    // Escribe la solicitud en el pipe
	ssize_t bytes_written = write(fd_CS, &req, sizeof(Requerimiento));
    if (bytes_written == -1) {
        perror("Error al escribir en el FIFO");
        close(fd_CS);
        close(fd_SC);
        exit(1);
    }

    char *msg = (char *)malloc(256 * sizeof(char));
    memset(msg, 0, 256);
	// Lee la respuesta del servidor desde el pipe
    int read_bytes = read(fd_SC, msg, 256);
    if (read_bytes == -1) {
        perror("Error al leer del FIFO");
        close(fd_SC);
        close(fd_CS);
        free(msg);
        exit(1);
    }
    msg[strlen(msg)] = '\0';
    printf("\nRespuesta: %s\n", msg);
    free(msg);
}

// Función que lee el archivo de datos y envía las solicitudes al servidor
void leerArchivo(const char *fileDatos, int fd_CS, int fd_SC) {
    FILE *entrada = fopen(fileDatos, "r");
    if (entrada == NULL) {
        perror("Error al abrir el archivo de datos");
        close(fd_CS);
        close(fd_SC);
        exit(1);
    }

    Requerimiento req;
    char linea[100];
	// Lee cada línea del archivo de datos y envía la solicitud al servidor
    while (fgets(linea, sizeof(linea), entrada)) {
        if (linea[strlen(linea) - 1] == '\n') {
            linea[strlen(linea) - 1] = '\0';
        }
        if (sscanf(linea, "%c, %29[^,], %29[^,]\n", &req.operacion, req.nombre, req.isbn) == 3) {
            printf("Operacion: %c, Nombre: %s, ISBN: %s", req.operacion, req.nombre, req.isbn);
            enviarRequerimiento(fd_CS, fd_SC, req.operacion, req.nombre, req.isbn);
            if (req.operacion == 'Q') {
                printf("\nGracias por usar nuestro sistema\n");
                fclose(entrada);
                close(fd_CS);
                close(fd_SC);
                exit(0);
            }
        }
    }
    fclose(entrada);
}

// Función que maneja la opción de realizar otra solicitud
int manejarOtraOpcion(int fd_CS, int fd_SC) {
    char buffer[256];
    int valido = 1;
    while (valido) {
        printf("Quieres ingresar otra solicitud? (s/n)\n");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            perror("Error al leer mensaje");
            continue;
        }
        if (buffer[strlen(buffer) - 1] == '\n') {
            buffer[strlen(buffer) - 1] = '\0';
        }
        if (strcmp(buffer, "n") == 0) {		// Si el usuario no quiere continuar, envía una señal de salida (Q)
            Requerimiento req = {'Q', "-", "-"};
            ssize_t bytes_written = write(fd_CS, &req, sizeof(Requerimiento));
            if (bytes_written == -1) {
                perror("Error al escribir en el FIFO");
            }
            printf("\nGracias por usar nuestro sistema\n");
            close(fd_CS);
            close(fd_SC);
            return 0;
        } else if (strcmp(buffer, "s") == 0) {
            valido = 0;
            printf("\n");
        } else {
            printf("Entrada invalida, por favor ingrese 's' o 'n'\n\n");
        }
    }
    return 1;
}
