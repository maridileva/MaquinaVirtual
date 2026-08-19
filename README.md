Emulador de Máquina Virtual en C
Emulador e intérprete de máquina virtual desarrollado en C. Simula registros, memoria principal y el ciclo de ejecución de instrucciones (fetch-decode-execute).
 
Características
- Decodificación y ejecución de instrucciones binarias/opcodes.
- Manejo de memoria y registros internos.
- Arquitectura modular (`main.c`, `mv.c`, `mv.h`).

Compilación y Ejecución
bash
gcc -o mv main.c mv.c
./mv programa.vmx
