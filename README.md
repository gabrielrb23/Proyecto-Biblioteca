# Sistema de Gestión de Biblioteca - Cliente/Servidor

## Descripción
Sistema cliente-servidor para gestión de préstamos de libros implementado en C, con comunicación mediante pipes FIFO y procesamiento concurrente. El servidor (RP) gestiona la base de datos de libros y el cliente (PS) proporciona la interfaz de usuario.

## Compilación y Ejecución

```bash
# Compilar (usando Makefile)
make

# Ejecutar Servidor (RP)
./rp -p nombre_pipe -f archivo_bd [-v] [-s archivo_salida]

# Ejecutar Cliente (PS)
./ps -p nombre_pipe [-i archivo_entrada]
