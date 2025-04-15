# Makefile para compilar los programas ps (cliente) y rp (servidor)

# Variables
CC = gcc                  # Compilador
CFLAGS = -Wall -g         # Opciones de compilación: advertencias y depuración
BIN_CLIENTE = ps          # Nombre del ejecutable del cliente
BIN_SERVIDOR = rp         # Nombre del ejecutable del servidor
SRC_CLIENTE = ps.c        # Código fuente del cliente
SRC_SERVIDOR = rp.c       # Código fuente del servidor

# Regla por defecto: compilar ambos programas
all: $(BIN_CLIENTE) $(BIN_SERVIDOR)

# Regla para compilar el cliente (ps)
$(BIN_CLIENTE): $(SRC_CLIENTE)
	$(CC) $(CFLAGS) $(SRC_CLIENTE) -o $(BIN_CLIENTE)

# Regla para compilar el servidor (rp)
$(BIN_SERVIDOR): $(SRC_SERVIDOR)
	$(CC) $(CFLAGS) $(SRC_SERVIDOR) -o $(BIN_SERVIDOR)

# Limpiar los archivos generados
clean:
	rm -f $(BIN_CLIENTE) $(BIN_SERVIDOR)

.PHONY: all clean
