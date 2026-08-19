#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mv.h"

uint8_t versionPrograma = 0;
int continuarEjecucion = 1;//para controlar el bucle
char *archivo_vmi=NULL;
extern uint32_t TAMANIO_MEMORIA;
extern uint32_t entryPoint;

void mostrarUso() {
    printf("Uso: mvx [archivo.vmx] [archivo.vmi] [m=M] [-d] [-p param1 param2 ...] \n");
    printf("Opciones:\n");
    printf("  archivo.vmx   : Archivo de programa a cargar \n");
    printf("  archivo.vmi   : Guardar/Cargar estado de la MV \n");
    printf("  m=M           : Tamanio de la memoria principal (Opcional, 16KiB por defecto) \n");
    printf("  -d            : Mostrar desensamblado \n");
    printf("  -p param...   : Parametros para el programa \n");
}

int main(int argc, char *argv[]) {
    //Procesamient de argumentos
    int desensamblar = 0;
    const char *archivo_vmx = NULL;
    char **parametros = NULL;
    int cantParam = 0;
    srand(time(NULL)); // Para la instruccion RND

    uint32_t TAMANIO_MEMORIA_KiB; //acceso a la variable global

    printf("Maquina Virtual MV1 y MV2 - UNMDP - Arquitectura de Computadoras\n");

    for (int i = 1; i < argc; i++) {
        if(archivo_vmx == NULL && strstr(argv[i], ".vmx")){
            archivo_vmx = argv[i];
        }else if(archivo_vmi == NULL && strstr(argv[i], ".vmi")){
            archivo_vmi = argv[i];
        }else if(strncmp(argv[i], "m=", 2) ==0){
            TAMANIO_MEMORIA_KiB = atoi(argv[i]+2);
            if(TAMANIO_MEMORIA_KiB < 1 || TAMANIO_MEMORIA_KiB > 1024){
                fprintf(stderr, "Error: Tamanio de memoria invalido. Debe ser entre 1 y 1024 KiB.\n");
                return 1;
            }
            TAMANIO_MEMORIA = TAMANIO_MEMORIA_KiB * 1024; // Convertir a bytes
        }else if(strcmp(argv[i], "-d") == 0){
            desensamblar = 1;
        }else if(strcmp(argv[i], "-p") == 0){
            for(int j = i+1; j<argc; j++){
                parametros = realloc(parametros, (cantParam+1)*sizeof(char*));
                parametros[cantParam] = argv[j];
                cantParam++;
            }
            break; // Salir del bucle una vez que se procesan los parámetros
        }
    }

    //Verifica que haya al menos un archivo de programa
    if (archivo_vmx == NULL && archivo_vmi == NULL) {
        mostrarUso();
        return 1;
    }

    if(archivo_vmi !=NULL && archivo_vmx == NULL){
        //Solo carga imagen
        inicializaMemoria();
        if(cargarImagenVMI(archivo_vmi) !=0){
            fprintf(stderr, "Error al cargar la imagen VMI \n");
            if(parametros!=NULL)
                free(parametros);
            return 1;
        }
    }else{
        //Inicializar memoriay cargar programa
        inicializaMemoria();
        if (cargaPrograma(archivo_vmx, parametros, cantParam) != 0) {
            fprintf(stderr, "Error al cargar el programa\n");
            if(parametros!=NULL)
                free(parametros);
        return 1;
        }
    }

    // Modo desensamblado
    if (desensamblar) {
        muestraDesensamblador(versionPrograma);
        if(parametros!=NULL)
                free(parametros);
        return 0;
    }
    // Ejecutar
    int resultado = ejecutarPrograma();

    // Limpieza
    if(parametros!=NULL){
        free(parametros);
    }

    return resultado;
}
