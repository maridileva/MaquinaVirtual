#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h> //para tener INT_MAX e INT_MIN
#include "mv.h"

//-------------VARIABLES GLOBALES---------------
// Memoria principal
uint8_t *MemoriaPrincipal;
uint32_t TAMANIO_MEMORIA = 16384; //tamanio en bytes por defecto
uint32_t entryPoint = 0; // Entry point del programa
// Tabla de Registros
uint32_t Registros[NUM_REGISTROS];
DescriptoresSegmentos tablaSegmentos[NUM_DESC];

//variables del main
extern uint8_t versionPrograma;
extern int continuarEjecucion;//para controlar el bucle
extern char *archivo_vmi;

//---------------FUNCION PARA DETECCION DE ERROR---------------
void detectaError(int8_t cod, int32_t er){
    continuarEjecucion=0;
    switch(cod){
        case COD_ERR_DIV: {
            printf("Error, dividendo es cero \n");
            break;
            }
        case COD_ERR_INS: {
            printf("Error, instruccion invalida \n");
            break;
        }
        case COD_ERR_REG:{
            printf("Error, registro invalido \n");
            break;
        }
        case COD_ERR_FIS: {
            printf("Error, direccion fisica invalida: 0x%08X \n",er);
            break;
        }
        case COD_ERR_OPE:{
            printf("Error, operando invalido \n");
            break;
        }
        case COD_ERR_READ:{
            printf("Error, modo de lectura invalida \n");
            break;
        }
        case COD_ERR_WRITE:{
            printf("Error, modo de escritura invalido \n");
            break;
        }
        case COD_ERR_OVF:{
            printf("Error, operacion genera overflow 0x%08X \n",er);
            break;
        }
        case COD_ERR_LOG:{
            printf("Error, direccion logica invalida: 0x%08X \n",er);
            break;
        }
        case COD_ERR_MEM_INS:{
            printf("Error, tamanio de memoria insuficiente para guardar el programa. Tamanio memoria: %d, tamanio programa: %d \n",TAMANIO_MEMORIA,er);
            break;
        }
        case COD_ERR_STACK_OVF:{
            printf("Error, stack overflow 0x%08X \n",er);
            break;
        }
        case COD_ERR_STACK_UDF:{
            printf("Error, stack underflow 0x%08X \n",er);
            break;
        }
        case COD_ERR_SEGMENT:{
            printf("Error, segmento invalido 0x%08X \n",er);
            break;
        }
        case COD_ERR_STACK:{
            printf("Error, no hay segmento stack\n");
            break;
        }
    }
}

//--------------DECLARACIONES DE FUNCIONES PARA VIRTUAL MACHINE------//
void inicializaMemoria(){
    if (MemoriaPrincipal != NULL) {
        free(MemoriaPrincipal);
    }

    MemoriaPrincipal = malloc(TAMANIO_MEMORIA);
    if (!MemoriaPrincipal) {
        exit(EXIT_FAILURE);
    }

    memset(MemoriaPrincipal, 0, TAMANIO_MEMORIA);
}


//---------------- FUNCIONES DE MEMORIA ----------------
uint32_t calculaDireccionFisica(uint32_t direccionLogica) {
    uint16_t segmento = direccionLogica >> 16;
    uint16_t offset = direccionLogica & 0xFFFF;

    if (segmento >= NUM_DESC) {
        detectaError(COD_ERR_LOG, direccionLogica);
        return 0;
    }
    // Verificar que el desplazamiento no exceda el tamanio del segmento
    if (offset >= tablaSegmentos[segmento].tamanio) {
        detectaError(COD_ERR_LOG, direccionLogica);
        return 0;
    }
    uint32_t direccionFisica = tablaSegmentos[segmento].base + offset;

    if (direccionFisica >= TAMANIO_MEMORIA) {
        detectaError(COD_ERR_FIS, direccionFisica);
        return 0;
    }
    return direccionFisica;
}

int32_t leerMemoria(uint32_t direccionFisica, uint8_t tamanio) {
    if(direccionFisica + tamanio > TAMANIO_MEMORIA) {
        detectaError(COD_ERR_FIS, direccionFisica + tamanio);
        return 0;
    }

    int32_t valor = 0;
    for(int i = 0; i < tamanio; i++) {
        valor = (valor << 8) | MemoriaPrincipal[direccionFisica + i];
    }

    // Extensión de signo para tamaños menores a 4 bytes
    if(tamanio < 4) {
        int bits = tamanio * 8;
        valor = (valor << (32 - bits)) >> (32 - bits);
    }

    return valor;
}

void escribirMemoria(uint32_t direccionFisica,int32_t valor, uint8_t tamanio) {
    if(direccionFisica+tamanio > TAMANIO_MEMORIA) {
        detectaError(COD_ERR_FIS,direccionFisica+tamanio);
        return;
    }
     for (int i = 0; i < tamanio; i++) {
        MemoriaPrincipal[direccionFisica + i] = (valor >> (8 * (tamanio - 1 - i))) & 0xFF;
    }

}

//----------------INICIALIZACIONES PARA VERSION 1---------------------
void inicializaSegmentosV1(uint16_t tamanioCodigo){
    for(int i=0; i<NUM_DESC; i++){
        tablaSegmentos[i].base = 0;
        tablaSegmentos[i].tamanio = 0;
    }
        //actualiza la tabla de descriptores
        //En esta version, POS_CS y POS_DS coinciden con las pos de registros
    tablaSegmentos[POS_CS].tamanio = tamanioCodigo;

    tablaSegmentos[POS_DS].base = tamanioCodigo;
    tablaSegmentos[POS_DS].tamanio = TAMANIO_MEMORIA-tamanioCodigo;
 }

void inicializaTablaRegistrosV1(){
    memset(Registros, 0, sizeof(Registros)); // Inicializo registros a 0

    // CS: segmento 0x0000, offset 0x0000
    Registros[POS_CS] = 0x00000000; // o podria ser POS_CS<<16
    // DS: segmento 0x0001, offset 0x0000
    Registros[POS_DS] = 0x00010000; // o podria ser POS_DS<<16
    // IP: comienza en 0
    Registros[POS_IP] = 0x00000000;
}

//------------FUNCIONES PARA PARAM SEGMENT----------------
uint32_t calculaTamanioParametros(char **parametros, int cantParam){
    uint32_t tamanio_ParamSeg = 0;

    //Calcular tamanio de strings mas terminadores
    for(int i=0; i<cantParam; i++){
        tamanio_ParamSeg += strlen(parametros[i]) +1;
    }

    //Aniadir espacio para el array de punteros (4 bytes por parametro)
    tamanio_ParamSeg += cantParam*4;

    return tamanio_ParamSeg;
}

uint32_t inicializaParamSegment(char **parametros, int cantParam){
    uint32_t pos_act = tablaSegmentos[0].base;

    uint32_t argv_dir_start = pos_act;

    //Calcular espacio total para strings
    for(int i=0; i<cantParam; i++){
        argv_dir_start += strlen(parametros[i])+1; //+1 para el '/0'
    }

    //Escribir strings y construir array de punteros
    uint32_t act_str_offset = pos_act;
    for(int i=0; i<cantParam; i++){
        //Copiar string a memoria
        strcpy((char*)MemoriaPrincipal + act_str_offset, parametros[i]);

        //Escribir puntero en array argv
        uint32_t offsetRel = act_str_offset - pos_act; //offset relativo al seg
        escribirMemoria(argv_dir_start + i*4, offsetRel, sizeof(uint32_t));
        act_str_offset +=strlen(parametros[i])+1;
    }
    return argv_dir_start; // Devuelve la dirección del array argv
}

//------------------INICIALIZACIONES PARA VERSION 2------------------------------------
void inicializaTablasV2(VMXHeaderV2 encabezado, char **parametros, int cantParam) {
    uint32_t pos_act = 0;
    int contSeg = 0; // Contador de segmentos
    memset(Registros, 0, sizeof(Registros));
    uint32_t dir_argv_mv = 0xFFFFFFFF; // Inicializar a un valor inválido

    // 1. PARAM SEGMENT (si hay parámetros)
    if (cantParam > 0) {
        uint32_t tamanio_param = calculaTamanioParametros(parametros, cantParam);
        if(contSeg<=NUM_DESC){
            tablaSegmentos[contSeg].base = pos_act;
            tablaSegmentos[contSeg].tamanio = tamanio_param;
            dir_argv_mv = inicializaParamSegment(parametros, cantParam);
            pos_act += tamanio_param;
            contSeg++;
        }
        else
            return;

    }

    // 2. CONST SEGMENT (si existe)
    if (encabezado.tamanio_const > 0) {
        tablaSegmentos[contSeg].base = pos_act;
        tablaSegmentos[contSeg].tamanio = encabezado.tamanio_const;
        pos_act += encabezado.tamanio_const;
        Registros[POS_KS]= (contSeg<<16);
        contSeg++;
    }
    else{
        Registros[POS_KS]=0xFFFFFFFF;
    }

    // 3. CODE SEGMENT (siempre existe)
    tablaSegmentos[contSeg].base = pos_act;
    tablaSegmentos[contSeg].tamanio = encabezado.tamanio_cod;
    pos_act += encabezado.tamanio_cod;
    Registros[POS_CS]=(contSeg<<16);
    contSeg++;

    // 4. DATA SEGMENT (si existe)
    if (encabezado.tamanio_datos > 0) {
        tablaSegmentos[contSeg].base = pos_act;
        tablaSegmentos[contSeg].tamanio = encabezado.tamanio_datos;
        pos_act += encabezado.tamanio_datos;
        Registros[POS_DS]=(contSeg<<16);
        contSeg++;
    }
    else{
        Registros[POS_DS]=0xFFFFFFFF;
    }

    // 5. EXTRA SEGMENT (si existe)
    if (encabezado.tamanio_extra > 0) {
        tablaSegmentos[contSeg].base = pos_act;
        tablaSegmentos[contSeg].tamanio = encabezado.tamanio_extra;
        pos_act += encabezado.tamanio_extra;
        Registros[POS_ES]=(contSeg<<16);
        contSeg++;
    }
    else{
        Registros[POS_ES]=0xFFFFFFFF;
    }

    // 6. STACK SEGMENT (si existe)
    if (encabezado.tamanio_stack > 0) {
        tablaSegmentos[contSeg].base = pos_act;
        tablaSegmentos[contSeg].tamanio = encabezado.tamanio_stack;
        Registros[POS_SS] = (contSeg << 16);
        //El SP debe apuntar al "tope" de la pila, que es el final del segmento de stack.
        //Pero la pila crece hacia abajo. Entonces, el SP inicial es la base + el tamaño.
        Registros[POS_SP] = (contSeg<<16) | encabezado.tamanio_stack;
        Registros[POS_BP] = Registros[POS_SP];
        contSeg++;
    }
    else{
        Registros[POS_SS] = 0xFFFFFFFF;
        Registros[POS_SP] = 0;
    }

    // Inicializar segmentos no usados
    for (; contSeg < NUM_DESC; contSeg++) {
        tablaSegmentos[contSeg].base = 0;
        tablaSegmentos[contSeg].tamanio = 0;
    }

    // Inicializar IP con el entry point
    Registros[POS_IP] = (Registros[POS_CS] & 0xFFFF0000) | encabezado.entry_point;
    inicializarPilaMain(cantParam, dir_argv_mv);

}

//-------------------LECTURA DEL ARCHIVO---------------------------------
    //-----------CARGA PROGRAMA PARA VERSION 1----------------------//
int cargaProgramaV1(FILE *arch) {
    VMXHeaderV1 encabezado;

    // Leer header completo
    if (fread(&encabezado, sizeof(VMXHeaderV1), 1, arch) != 1) {
        printf("Error al leer encabezado MV1\n");
        return -1;
    }


    // Verificar identificador y versión
    if (memcmp(encabezado.identificador, "VMX25", 5) != 0 || encabezado.version != 1) {
        printf("Encabezado inválido para MV1\n");
        return -1;
    }

    encabezado.tamanio = convertirBigEndian16(encabezado.tamanio);

    // Verificar que el programa cabe en memoria
    if (encabezado.tamanio == 0 || encabezado.tamanio >= TAMANIO_MEMORIA ) {
        detectaError(COD_ERR_MEM_INS, encabezado.tamanio);
        fclose(arch);
        return -1;
    }

    // Leer código directamente al inicio de la memoria
    size_t bytes_leidos = fread(MemoriaPrincipal, sizeof(uint8_t), encabezado.tamanio, arch);
    if (bytes_leidos != encabezado.tamanio) {
        printf("Error: tamaño del código no coincide\n");
        fclose(arch);
        return -1;
    }

    fclose(arch);

    // Inicializar segmentos y registros
    inicializaSegmentosV1(encabezado.tamanio);
    inicializaTablaRegistrosV1();

    return 0;
}
    //----------------CARGA PROGRAMA PARA VERSION 2-----------------------
int cargaProgramaV2(FILE *arch, char **parametros, int cantParam) {
    VMXHeaderV2 encabezado;

    // Leer header completo
    if (fread(&encabezado, sizeof(VMXHeaderV2), 1, arch) != 1) {
        printf("Error al leer encabezado MV2\n");
        return -1;
    }

    encabezado.tamanio_cod = (encabezado.tamanio_cod >> 8) | (encabezado.tamanio_cod << 8);
    encabezado.tamanio_datos = (encabezado.tamanio_datos >> 8) | (encabezado.tamanio_datos << 8);
    encabezado.tamanio_extra = (encabezado.tamanio_extra >> 8) | (encabezado.tamanio_extra << 8);
    encabezado.tamanio_stack = (encabezado.tamanio_stack >> 8) | (encabezado.tamanio_stack << 8);
    encabezado.tamanio_const = (encabezado.tamanio_const >> 8) | (encabezado.tamanio_const << 8);
    encabezado.entry_point = (encabezado.entry_point >> 8) | (encabezado.entry_point << 8);

    entryPoint = encabezado.entry_point;


    // Validar tamaños no nulos
    if (encabezado.tamanio_cod == 0) {
        return -1;
    }

    // Verificar que ningún segmento tenga tamaño negativo disfrazado (por cast incorrecto)
    if (encabezado.tamanio_cod > TAMANIO_MEMORIA ||
        encabezado.tamanio_datos > TAMANIO_MEMORIA ||
        encabezado.tamanio_extra > TAMANIO_MEMORIA ||
        encabezado.tamanio_stack > TAMANIO_MEMORIA ||
        encabezado.tamanio_const > TAMANIO_MEMORIA) {
        return -1;
    }

    // Validar header
    if (strncmp(encabezado.identificador, "VMX25", 5) != 0 || encabezado.version != 2) {
        printf("Error: archivo no es MV2 válido\n");
        return -1;
    }

    if (entryPoint >= encabezado.tamanio_cod) {
        return -1;
    }
    // Calcular tamaño total necesario
    uint32_t total_size = encabezado.tamanio_cod+encabezado.tamanio_datos+encabezado.tamanio_extra+encabezado.tamanio_stack+encabezado.tamanio_const;

    if (cantParam > 0) {
        total_size += calculaTamanioParametros(parametros, cantParam);
    }

    if (total_size > TAMANIO_MEMORIA) {
        detectaError(COD_ERR_MEM_INS, total_size);
        return -1;
    }

    // Inicializar segmentos y registros
    inicializaTablasV2(encabezado, parametros,cantParam);


    // Cargar segmentos en memoria
    uint8_t posCS = Registros[POS_CS] >> 16;
    if (posCS >= NUM_DESC) {
        detectaError(COD_ERR_SEGMENT, posCS);
        return -1;
    }
    if (fread(MemoriaPrincipal + tablaSegmentos[posCS].base, 1, encabezado.tamanio_cod, arch) != encabezado.tamanio_cod) {
        printf("Error al leer Code Segment\n");
        return -1;
    }

    // Cargar Const Segment si existe
    if (encabezado.tamanio_const > 0 && Registros[POS_KS] != 0xFFFFFFFF) {
        uint8_t posKS = Registros[POS_KS] >> 16;
        if (fread(MemoriaPrincipal + tablaSegmentos[posKS].base, 1, encabezado.tamanio_const, arch) != encabezado.tamanio_const) {
            printf("Error al leer Const Segment\n");
            return -1;
        }
    }

    printf("Programa MV2 cargado correctamente\n");
    fclose(arch);
    return 0;
}


//----------------CARGA PROGRAMA SEGUN LA VERSION---------------
int cargaPrograma(const char *nombreArchivo, char **parametros, int cantParam) {
    FILE *arch;
    char identificador[6] = {0};

    printf("Intentando abrir archivo: '%s'\n", nombreArchivo);
    arch = fopen(nombreArchivo, "rb");
    if (arch == NULL) {
        printf("Error: no se pudo abrir el archivo\n");
        return -1;
    }

    // Leer identificador y versión
    if (fread(identificador, 1, 6, arch) != 6) {
        printf("Error: no se pudo leer la cabecera\n");
        fclose(arch);
        return -1;
    }

    // Volver al inicio para que las funciones específicas lean desde cero
    fseek(arch, 0, SEEK_SET);

    if (strncmp(identificador, "VMX25", 5) != 0) {
        printf("Error: encabezado desconocido (no es VMX25)\n");
        fclose(arch);
        return -1;
    }

    versionPrograma = (uint8_t)identificador[5];

    if (versionPrograma == 1) {
        printf("Archivo detectado como MV1\n");
        return cargaProgramaV1(arch);
    } else if (versionPrograma == 2) {
        printf("Archivo detectado como MV2\n");
        return cargaProgramaV2(arch, parametros, cantParam);
    } else {
        printf("Versión de archivo no soportada: %d\n", versionPrograma);
        fclose(arch);
        return -1;
    }
}
//-----------------CARGA O CREA ARCHIVO VMI-------------------
int cargarImagenVMI(const char *filename){
    FILE *vmi_file = fopen(filename, "rb");
    if(vmi_file == NULL){
        printf("Error: No se pudo abrir el archivo .vmi \n");
        return -1;
    }

    VMIHeader encabezado;
    if(fread(&encabezado, sizeof(VMIHeader), 1, vmi_file) !=1){
        fclose(vmi_file);
        printf("Error: No se pudo leer el encabezado .vmi\n");
        return -1;
    }

    if(memcmp(encabezado.identificador, "VMI25", 5) !=0){
        fclose(vmi_file);
        printf("Error: Formato de archivo .vmi no valido \n");
        return -1;
    }
    encabezado.tamanio_mem = convertirBigEndian16(encabezado.tamanio_mem);
    TAMANIO_MEMORIA = encabezado.tamanio_mem*1024; // Convertir a bytes
    printf("Header: %s, Version: %d, Tamanio Memoria: %d KiB\n",encabezado.identificador, encabezado.version, encabezado.tamanio_mem);

    if(fread(Registros, sizeof(uint32_t), NUM_REGISTROS, vmi_file) !=NUM_REGISTROS){
        fclose(vmi_file);
        printf("Error: No se pudieron leer los registros \n");
        return -1;
    }

    if(fread(tablaSegmentos,sizeof(DescriptoresSegmentos), NUM_DESC,vmi_file) !=NUM_DESC){
        fclose(vmi_file);
        printf("Error: No se pudieron leer la tabla de segmentos \n");
        return -1;
    }

    //Muestra registros
    printf("Registros:\n");
    for (int i = 0; i < NUM_REGISTROS; i++) {
        if(Registros[i] != 0xFFFFFFFF){
            Registros[i]=convertirBigEndian32(Registros[i]);
        }
    }
    //Muestra segmentos
    printf("Segmentos:\n");
    for (int i = 0; i < NUM_DESC; i++) {
        if(tablaSegmentos[i].base == 0xFFFF && tablaSegmentos[i].tamanio == 0xFFFF){
            tablaSegmentos[i].tamanio = 0;
            tablaSegmentos[i].base = 0;
        }else{
           tablaSegmentos[i].tamanio = convertirBigEndian16(tablaSegmentos[i].tamanio);
           tablaSegmentos[i].base    = convertirBigEndian16(tablaSegmentos[i].base);
        }
    }

    if(fread(MemoriaPrincipal, 1, TAMANIO_MEMORIA, vmi_file) != TAMANIO_MEMORIA){
        fclose(vmi_file);
        return -1;
    }
    fclose(vmi_file);
    return 0;
}

int guardarImagenVMI(const char *filename){
    int i;
    uint16_t base_vmi,tam_vmi;
    FILE *vmi_file = fopen(filename, "wb");
    if(vmi_file == NULL){
        printf("Error: No se pudo crear el archivo \n");
        return -1;
    }

    VMIHeader encabezado;
    memcpy(encabezado.identificador, "VMI25", 5);
    encabezado.version = 1;
    encabezado.tamanio_mem = convertirBigEndian16(TAMANIO_MEMORIA / 1024);

    if(fwrite(&encabezado, sizeof(VMIHeader), 1, vmi_file) != 1){
        fclose(vmi_file);
        return -1;
    }

    for ( i = 0; i < NUM_REGISTROS; i++) {
        uint32_t reg_be = convertirBigEndian32(Registros[i]);
        fwrite(&reg_be, sizeof(uint32_t), 1, vmi_file);
    }
    for(i=0;i<NUM_DESC;i++){
        base_vmi= convertirBigEndian16(tablaSegmentos[i].base);
        tam_vmi= convertirBigEndian16(tablaSegmentos[i].tamanio);
        fwrite(&base_vmi, sizeof(uint16_t), 1, vmi_file);
        fwrite(&tam_vmi, sizeof(uint16_t), 1, vmi_file);
    }

    if (fwrite(MemoriaPrincipal, 1, TAMANIO_MEMORIA, vmi_file) != TAMANIO_MEMORIA) {
        fclose(vmi_file);
        return -1;
    }

    fclose(vmi_file);
    printf("Estado de la MV guardado en %s\n",filename);
    return 0;
}

//-----------------FUNCIONES DE REGISTROS--------------
int verificaRegistro(uint8_t numReg, uint8_t sector) {
    if (numReg >= NUM_REGISTROS || sector > 3) {
        detectaError(COD_ERR_REG,numReg);
        return -1;
    }
    return 0;
}

int32_t obtenerValorRegistro(uint8_t numReg, uint8_t sector) {
    int32_t valor = (int32_t)Registros[numReg];

    if (numReg == POS_DS) {
        return valor;
    }

    switch(sector) {
        case 0x00: return valor; //Obtener el valor completo del registro
        case 0x01: return (int8_t)(valor & 0xFF); //Obtener la parte baja del registro
        case 0x02: return (int8_t)((valor >> 8) & 0xFF);// Obtener la parte media del registro
        case 0x03: return (int16_t)(valor & 0xFFFF); // Obtener la parte alta del registro
        default:
            detectaError(COD_ERR_REG, numReg);
            return 0;
    }
}

void escribirEnRegistro(uint8_t numReg, uint8_t sector, int32_t valor) {
    //registro verificado en funcion que la invoca
    switch(sector){
        case 0x00:
            Registros[numReg] = valor;
            break; // Escribir el valor completo en el registro
        case 0x01:
            Registros[numReg] = (Registros[numReg] & 0xFFFFFF00) | (valor & 0xFF);
            break; // Escribir en la parte baja del registro
        case 0x02:
            Registros[numReg] = (Registros[numReg] & 0xFFFF00FF) | ((valor & 0xFF) << 8);
            break; // Escribir en la parte media del registro
        case 0x03:
            Registros[numReg] = (Registros[numReg] & 0xFFFF0000) | (valor & 0xFFFF);
            break; // Escribir en la parte alta del registro
        default:
            detectaError(COD_ERR_REG,numReg);
            return;
    }
}

uint32_t calculaDireccionSalto(uint8_t tipoA, uint32_t operandoA,uint8_t tamA) {
    //los saltos son con respecto al Code Segment
    uint32_t direccionSalto;
    uint32_t offset;

    if(tipoA == OP_REG) {
        uint8_t numReg = (operandoA >> 4) & 0x0F;
        uint8_t sector = (operandoA >> 2) & 0x03;
        offset = obtenerValorRegistro(numReg, sector); // Obtener el offset que guarda el registro

    } else if(tipoA == OP_INM) {
        offset = operandoA;

    } else if(tipoA == OP_MEM) {
        uint32_t direccionFisica = calculaDireccionFisica(operandoA);
        offset = leerMemoria(direccionFisica, tamA); // Leer el offset de memoria

    } else {
        detectaError(COD_ERR_OPE,tipoA);
        return 0xFFFFFFFF;
    }
    // Verificar si la direccion de salto es valida
    if (offset >= tablaSegmentos[Registros[POS_CS]>>16].tamanio) {
        return 0xFFFFFFFF;
    }
    direccionSalto = (Registros[POS_CS] & 0xFFFF0000) | offset;
    return direccionSalto;
}


void actualizarCC(int32_t resultado) {
    Registros[POS_CC] &= ~(CC_N | CC_Z);
    // Bit N (signo)
    if (resultado < 0) {
        Registros[POS_CC] |= CC_N;
    }

    // Bit Z (cero)
    if (resultado == 0) {
        Registros[POS_CC] |= CC_Z;
    }

}

//---------------------FUNCIONES DE OPERANDOS------------------
uint32_t obtenerOperando(uint8_t tipo, unsigned int *ip, uint8_t *tam) {
    uint32_t operando = 0;
    uint8_t byte1, byte2, byte3;
    // Verifica si el operando es valido, y mueve el IP
    uint32_t ip_aux=calculaDireccionFisica(*ip);

    *tam=4;
    if(tipo == OP_REG) { // Operando de registro (1 byte)
        byte1 = MemoriaPrincipal[ip_aux];
        (*ip)++; ip_aux++;
        if(verificaRegistro((byte1 >> 4) & 0x0F, (byte1 >> 2) & 0x03) == -1) {
            detectaError(COD_ERR_REG,(byte1 >> 4) & 0x0F);
            return 0;
        }
        operando = byte1;

    }else if(tipo == OP_INM) { // Operando inmediato (2 bytes)
        byte1 = MemoriaPrincipal[ip_aux];
        byte2 = MemoriaPrincipal[ip_aux + 1];
        (*ip) += 2; ip_aux += 2;
        operando = (byte1 << 8) | byte2; // Ensamblar el valor inmediato de 16 bits
        // Si el valor es negativo, extiendo para signo
        if (operando & 0x8000) {
            operando |= 0xFFFF0000;
        }
    }else if(tipo == OP_MEM) { // Operando de memoria (3 bytes)
        byte1 = MemoriaPrincipal[ip_aux];
        byte2 = MemoriaPrincipal[ip_aux + 1];
        byte3 = MemoriaPrincipal[ip_aux + 2];
        (*ip) += 3; ip_aux += 3;

        //Determinar tamanio de acceso:
        uint8_t mod=byte3 & 0x03;
        switch(mod){
            case MOD_LONG: *tam=4; break;
            case MOD_WORD: *tam=2; break;
            case MOD_BYTE: *tam=1; break;
        }

        uint8_t codReg = (byte3 >> 4) & 0x0F; // Extraer posicion del registro que guarda la memoria
        uint16_t offsetReg = (Registros[codReg]) & 0xFFFF; // Extraer el offset del registro
        uint16_t offset = (byte1 << 8) | byte2; // Ensambla el offset de 16 bits
        // Calcular direccion logica
        uint32_t base = Registros[codReg] >>16; // Extraer el segmento
        uint32_t direccionLogica = (base << 16) | (offset+offsetReg); // Ensambla la direccion logica

        operando = direccionLogica; //Devuelve DIRECCION LOGICA
    }else {
        detectaError(COD_ERR_OPE,tipo);
        return 0;
    }
    return operando;
}

int32_t obtenerValorOperando(uint8_t tipoOp, uint32_t operando, uint8_t tamanio){
    int32_t valor = 0;

    if (tipoOp == OP_REG) {
        uint8_t numReg = (operando >> 4) & 0x0F;
        uint8_t sector = (operando >> 2) & 0x03;
        valor =(int32_t) obtenerValorRegistro(numReg, sector);

    } else if (tipoOp == OP_INM) {
                valor = (int32_t)operando;
    } else if (tipoOp == OP_MEM) {
        uint32_t direccionFisica = calculaDireccionFisica(operando);
        valor = (int32_t) leerMemoria(direccionFisica, tamanio);
    } else {
        detectaError(COD_ERR_OPE, tipoOp);
        return 0;
    }

    return valor;
}

void escribirValorOperando(uint8_t tipoOp, uint32_t operando, int32_t valor,uint8_t tamA){

    if(tipoOp == OP_REG) {
        uint8_t numReg = (operando >> 4) & 0x0F;
        uint8_t sector = (operando >> 2) & 0x03;
        escribirEnRegistro(numReg, sector, valor);
    } else if(tipoOp == OP_MEM) {
        uint32_t direccionFisica = calculaDireccionFisica(operando);
        escribirMemoria(direccionFisica, valor, tamA);
    } else {
        detectaError(COD_ERR_OPE,tipoOp);
    }
}

//----------------FUNCIONES DE INSTRUCCIONES--------------------
void ejecutarMOV(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA,uint8_t tamB) {
    int32_t valor;

    valor = obtenerValorOperando(tipoB, operandoB, tamB);
    escribirValorOperando(tipoA, operandoA, valor, tamA);
    // MOV no afecta el registro CC
}

void ejecutarADD(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;
    // Obtener valor del operando B (fuente)
    valorB = obtenerValorOperando(tipoB, operandoB, tamB);

    // Obtener valor del operando A (destino)
    valorA = obtenerValorOperando(tipoA, operandoA, tamA);

    // Verificar overflow
    if ((valorB > 0 && valorA > INT32_MAX - valorB) || (valorB < 0 && valorA < INT32_MIN - valorB)) {
        detectaError(COD_ERR_OVF,0x0);
        return;
    }
    // Sumar los valores
    resultado = valorA + valorB;

    //Guardo resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarSUB(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;
    //fuente
    valorB = obtenerValorOperando(tipoB, operandoB, tamB);

    //destino
    valorA = obtenerValorOperando(tipoA, operandoA, tamA);

    // Verificar overflow
    if ((valorB > 0 && valorA < INT32_MIN + valorB) || (valorB < 0 && valorA > INT32_MAX + valorB)) {
        detectaError(COD_ERR_OVF,0x00);
        return;
    }

    // Restar los valores
    resultado = valorA - valorB;
    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarSWAP(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB){
    uint32_t valorA, valorB;
    //Verificar que los tamanios son compatibles
    if(tamA != tamB){
        detectaError(COD_ERR_OPE,0x0);
        return;
    }

    // Obtener valor del operando A
    valorA = obtenerValorOperando(tipoA, operandoA,tamA);
    // Obtener valor del operando B
    valorB = obtenerValorOperando(tipoB, operandoB,tamB);

    // Intercambiar los valores
    escribirValorOperando(tipoA, operandoA, valorB,tamB);
    escribirValorOperando(tipoB, operandoB, valorA,tamA);

    //SWAP no afecta el registro CC
}

void ejecutarMUL(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t resultado, valorA, valorB;
    //Fuente
    valorB = obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = obtenerValorOperando(tipoA, operandoA,tamA);

    // Verificar overflow
    if ((valorB > 0 && valorA > INT32_MAX / valorB) || (valorB < 0 && valorA < INT32_MIN / valorB)) {
        detectaError(COD_ERR_OVF,0x00);
        return;
    }
    // Multiplicar los valores
    resultado = valorA * valorB;
    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    actualizarCC(resultado); // Actualizar el registro de condicion (CC)
}

void ejecutarDIV(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t resultado, resto, valorA, valorB;

    //Fuente
    valorB = obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = obtenerValorOperando(tipoA, operandoA,tamA);

    // Verificar division por cero
    if (valorB == 0) {
        detectaError(COD_ERR_DIV,0x00); // Error en la division
        return;
    }
    // Dividir los valores
    resultado = valorA / valorB;
    resto = valorA % valorB; // Obtener el resto de la division

    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);

    // Guardar el resto en el registro (AC)
    Registros[POS_AC] = resto;
}

void ejecutarCMP(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;
    //SUB no guarda el valor en ningun registro ni memoria, solo actualiza el CC

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    // Restar los valores
    resultado = valorA - valorB;
    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarSHL(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);

    // Verificar que el valor B sea un desplazamiento valido
    if (valorB < 0 || valorB > 31) {
        detectaError(COD_ERR_OPE,0x00);
        return;
    }

    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    //Desplazar a la izquierda el valor A
    resultado = valorA << valorB;

    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarSHR(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);

    //Verificar que el valor B sea un desplazamiento valido
    if (valorB < 0 || valorB > 31) {
        detectaError(COD_ERR_OPE,0x0); // Error en el desplazamiento
        return;
    }

    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    //Desplazar a la derecha el valor A
    resultado = valorA >> valorB;
    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    //Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarAND(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    //Realizar la operacion AND
    resultado = valorA & valorB;
    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarOR(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    //Realizar la operacion OR
    resultado = valorA | valorB;

    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    //Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarXOR(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB, resultado;

    //Fuente
    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    //Destino
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    //Realizar la operacion XOR
    resultado = valorA ^ valorB;

    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarLDL(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB;

    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    int32_t resultado = (valorB & 0xFFFF) | (valorA & 0xFFFF0000);

    //Guardo el valor
    escribirValorOperando(tipoA, operandoA, resultado,tamA);

}

void ejecutarLDH(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t valorA, valorB;

    valorB = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);
    valorA = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);

    int32_t resultado = ((valorB & 0xFFFF) <<16 ) | (valorA & 0xFFFF);

    escribirValorOperando(tipoA, operandoA, resultado,tamA);
}

void ejecutarRND(uint8_t tipoA, uint32_t operandoA, uint8_t tipoB, uint32_t operandoB,uint8_t tamA, uint8_t tamB) {
    int32_t max;

    //Fuente
    max = (int32_t)obtenerValorOperando(tipoB, operandoB,tamB);

    // Generar un numero aleatorio entre 0 y valorB
    int32_t resultado = rand() % (max + 1);

    //Guardar resultado
    escribirValorOperando(tipoA, operandoA, resultado,tamA);
}

void ejecutarJMP(uint8_t tipoA, uint32_t operandoA,uint8_t tamA) {
    // Salta a la direccion especificada en el operando A
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if(direccionSalto != 0xFFFFFFFF) {
        Registros[POS_IP] = direccionSalto; // Actualizar el IP con la direccion de salto
    }
    else return;
}

void ejecutarJZ(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    // Salta a la direccion especificada en el operando A si el CC es cero
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if((Registros[POS_CC] & CC_Z) && direccionSalto!= 0xFFFFFFFF) {
        Registros[POS_IP] = direccionSalto;
    }
}

void ejecutarJP(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    // Salta a la direccion especificada en el operando A si el Z=0 y N=0
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if(!(Registros[POS_CC] & (CC_N | CC_Z)) && direccionSalto!= 0xFFFFFFFF) {
        Registros[POS_IP] = direccionSalto;
    }else return;
}

void ejecutarJN(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    // Salta a la direccion especificada en el operando A si el CC es negativo
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if((Registros[POS_CC] & CC_N) && direccionSalto!= 0xFFFFFFFF) {
        Registros[POS_IP] = direccionSalto;
    }
}

void ejecutarJNZ(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
// Salta a la direccion especificada si el resultado es distinta de cero
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);
    uint32_t valorCC = Registros[POS_CC]; // Obtener el valor del registro de condicion
    uint32_t cero= valorCC & CC_Z; // Obtener el valor del registro de condicion

    if(cero==00 && direccionSalto!= 0xFFFFFFFF) {
        Registros[POS_IP] = direccionSalto;
    }
}

void ejecutarJNP(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    // Salta a la direccion especificada si el resultado es <= cero
    uint32_t valorCC = Registros[POS_CC]; // Obtener el valor del registro de condicion
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if(((valorCC & CC_N) || (valorCC & CC_Z)) && direccionSalto != 0xFFFFFFFF){
        Registros[POS_IP] = direccionSalto;
    }
}

void ejecutarJNN(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    // Salta a la direccion especificada si el resultado es >= cero
    uint32_t valorCC = Registros[POS_CC]; // Obtener el valor del registro de condicion
    uint32_t direccionSalto = calculaDireccionSalto(tipoA, operandoA,tamA);

    if(!(valorCC & CC_N) && direccionSalto != 0xFFFFFFFF){
        Registros[POS_IP] = direccionSalto;
    }
}

void ejecutarNOT(uint8_t tipoA, uint32_t operandoA,uint8_t tamA){
    int32_t resultado, valor;

    valor = (int32_t)obtenerValorOperando(tipoA, operandoA,tamA);
    resultado = ~valor;

    escribirValorOperando(tipoA, operandoA, resultado,tamA);

    // Actualizar el registro de condicion (CC)
    actualizarCC(resultado);
}

void ejecutarPUSH(int32_t valorPush){ //al pasar valor, facilita guardar IP en la pila
    if (Registros[POS_SS] == 0xFFFFFFFF || versionPrograma==1 || (Registros[POS_SS] >> 16) >= NUM_DESC) {
        detectaError(COD_ERR_STACK, 0);
        return;
    }

    uint8_t segStack = Registros[POS_SS] >> 16;
    uint32_t nuevaSP = Registros[POS_SP] - 4;
    if((nuevaSP & 0xFFFF)>= tablaSegmentos[segStack].tamanio){
        detectaError(COD_ERR_STACK_OVF, nuevaSP & 0xFFFF);
        return;
    }
    uint32_t dirFis = calculaDireccionFisica(nuevaSP);

    // Verificar Stack Overflow
    if (dirFis < tablaSegmentos[segStack].base) {
        detectaError(COD_ERR_STACK_OVF, dirFis);
        return;
    }

    Registros[POS_SP] = nuevaSP;

    escribirMemoria(dirFis, valorPush, 4);
}

int32_t ejecutarPOP(int *error){
    if (Registros[POS_SS] == 0xFFFFFFFF || versionPrograma==1 || (Registros[POS_SS] >> 16) >= NUM_DESC) {
        *error=1;
        detectaError(COD_ERR_STACK, 0);
        return 0;
    }

    uint8_t segStack = Registros[POS_SS] >> 16;
    uint16_t offset = Registros[POS_SP] & 0xFFFF;

    if (offset + 4 > tablaSegmentos[segStack].tamanio) {
        *error=1;
        detectaError(COD_ERR_STACK_UDF, Registros[POS_SP]);
        return 0;
    }

    uint32_t dirFis = calculaDireccionFisica(Registros[POS_SP]);
    int32_t valor = leerMemoria(dirFis, 4);
    Registros[POS_SP] += 4;

    return valor;
}

void ejecutarCALL(uint32_t destino){
    // Verificar si el destino está dentro del Code Segment
    uint8_t pos_CS = Registros[POS_CS] >> 16;
    if (destino >= tablaSegmentos[pos_CS].tamanio) {
        detectaError(COD_ERR_SEGMENT, 0);
        return;
    }

    ejecutarPUSH(Registros[POS_IP]);
    Registros[POS_IP] = (Registros[POS_IP] & 0xFFFF0000) | (destino & 0xFFFF);
}

void ejecutarRET() {
    int error=0;
    uint32_t dirRET = ejecutarPOP(&error);
    if (error == 1) {
        return;
    }

    Registros[POS_IP] = dirRET;
}

//------------------INICIALIZO LA PILA PARA LA SUBRUTINA-----------
void inicializarPilaMain(int cantParam, uint32_t dirParam){
    //Verifico existencia de Stack Segment
    if(Registros[POS_SS] == 0xFFFFFFFF){
        return;
    }

    // Puntero a array de argumentos
    if(cantParam >0 && dirParam!=0){
        //Calcular posicion del array de punteros (al final del param segment)
        ejecutarPUSH(dirParam);
    }else{
        ejecutarPUSH(0xFFFFFFFF); //no hay parametros
    }

    // Cantidad de argumentos (argc)
    ejecutarPUSH(cantParam);

    // Direccion de retorno
    ejecutarPUSH(0xFFFFFFFF);
}

//LLAMADA A SYS
void writeSYS() {
    uint32_t EDX = Registros[POS_EDX];
    uint8_t CH = (Registros[POS_ECX] >> 8) & 0xFF; // Tamaño de cada elemento
    uint8_t CL = Registros[POS_ECX] & 0xFF;       // Cantidad de elementos
    uint8_t AL = Registros[POS_EAX] & 0xFF;       // Modo de salida
    uint32_t dirFisica = calculaDireccionFisica(EDX);

    // Verificación de parámetros
    if (CH != 1 && CH != 2 && CH != 4) {
        detectaError(COD_ERR_WRITE, CH);
        return;
    }

    if (dirFisica + (CH * CL) > TAMANIO_MEMORIA) {
        detectaError(COD_ERR_FIS, dirFisica + (CH * CL));
        return;
    }

    for (int i = 0; i < CL; i++) {
        uint32_t dirActual = dirFisica + (i * CH);
        printf("[%04X]:", dirActual);

        uint32_t valor = leerMemoria(dirActual, CH);

        // Mostrar hexadecimal
        if (AL & 0x08) {
            printf(" 0x%X", valor);
        }

        // Mostrar octal
        if (AL & 0x04) {
            printf(" 0o%o", valor);
        }

        // Mostrar caracteres
        if (AL & 0x02) {
            printf(" ");
            // Leer y mostrar los bytes en orden de memoria
            for (int j = 0; j < CH; j++) {
                uint8_t c = MemoriaPrincipal[dirActual+j];
                if (c >= 32 && c <= 126) {
                    printf("%c", c);
                } else {
                    printf(".");
                }
            }
        }

        // Mostrar decimal
        if (AL & 0x01) {
            printf(" ");
            switch (CH) {
                case 1: printf("%d", (int8_t)valor); break;
                case 2: printf("%d", (int16_t)valor); break;
                case 4: printf("%d", (int32_t)valor); break;
            }
        }
        printf("\n");
    }
}
void readSYS() {
    uint32_t EDX = Registros[POS_EDX], dirFisica;
    int32_t valor;
    uint8_t CH = (Registros[POS_ECX] >> 8) & 0xFF; // Tamaño de cada elemento
    uint8_t CL = Registros[POS_ECX] & 0xFF;        // Cantidad de elementos
    uint8_t AL = Registros[POS_EAX] & 0xFF;        // Tipo de entrada
    char binario[MAX];

    dirFisica = calculaDireccionFisica(EDX);
    if (dirFisica + (CH * CL) > TAMANIO_MEMORIA) {
        detectaError(COD_ERR_READ, AL);
        return;
    }

    for (int i = 0; i < CL; i++) {
        int intentoValido = 0,intento=0;
        uint32_t direccionMemoria = dirFisica + (i * CH);
        while(intento<3 && !intentoValido){
            printf("[%04X]: ", direccionMemoria);
            fflush(stdout); // Asegura que se muestre el prompt

            switch (AL) {
                case 0x10: { // BINARIO
                        if (scanf("%s", binario) == 1) {
                            valor = 0;
                            int j = 0;
                            while (binario[j] == '0' || binario[j] == '1') {
                                valor = (valor << 1) | (binario[j] - '0');
                                j++;
                            }
                            intentoValido=1;
                        }
                    break;
                }
                case 0x01: { // DECIMAL
                        if (scanf("%d", &valor) == 1) {
                            valor = (uint32_t)valor;
                            intentoValido=1;
                        }
                    break;
                }
                case 0x02: { // CARÁCTER
                        valor = getchar();
                        if (valor != '\n') intentoValido=1;
                    break;
                }
                case 0x04: { // OCTAL
                        if (scanf("%o", &valor) == 1) intentoValido=1;;
                    break;
                }
                case 0x08: { // HEX
                        if (scanf("%X", &valor) == 1) intentoValido=1;
                    break;
                }
                default:
                    detectaError(COD_ERR_READ, AL);
                    return;
            }
            while (getchar() != '\n'); // limpiar buffer de entrada

            if (!intentoValido && intento < 2) {
                    printf("Entrada inválida. Reintente.\n");
            }
            intento++;
        }
        if (!intentoValido) {
            detectaError(COD_ERR_READ, AL);
            return;
        }

        escribirMemoria(direccionMemoria, valor, CH);


    }
}



void writeSTR(){
    uint32_t EDX = Registros[POS_EDX];
    uint32_t dirFisica = calculaDireccionFisica(EDX);

    printf("%s",(char*)MemoriaPrincipal+dirFisica);
}

void readSTR() {
    uint32_t EDX = Registros[POS_EDX];
    uint16_t CX = Registros[POS_ECX] & 0xFFFF;

    // Limitar CX a 255 para evitar desbordamiento
    if (CX > 255) {
        CX = 255;
    }

    char cadena[256];

    if (fgets(cadena, CX + 1, stdin) == NULL) {
        cadena[0] = '\0'; // Si hay error de lectura, usamos cadena vacía
    }

    cadena[strcspn(cadena, "\n")] = '\0';

    uint32_t dirFisica = calculaDireccionFisica(EDX);

    size_t len = strlen(cadena);

    // Verificar que hay espacio en MemoriaPrincipal para escribir cadena + '\0'
    if (dirFisica + len >= TAMANIO_MEMORIA) {
        detectaError(COD_ERR_FIS, dirFisica + len);
        return;
    }

    // Copiar a memoria
    memcpy((char*)MemoriaPrincipal + dirFisica, cadena, len + 1); // Incluye '\0'
}


void mostrarMenu(char *op){
    printf("\n\tBREAKPOINT\t\n");
    printf("\t g: Continuar ejecucion\n");
    printf("\t q: Salir y mantener el breakpoint\n");
    printf("\t Enter: Ejecutar paso a paso\n");
    *op=getchar();
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

}
void breakPoint(){
    char op;
    int muestra=1,salir=0;
    while(continuarEjecucion != 0 && salir!=1){
        if(muestra==1)
            mostrarMenu(&op);
        else{
            op=getchar();
            while (getchar() != '\n');
        }

        switch(op){
            case 'g':{
                salir=1;
                break;
            }
            case 'q':{
                continuarEjecucion=0;
                break;
            }
            case '\n':{
                disassemblerPasoAPaso();
                ejecutarInstruccion();
                muestra=0;
                break;
            }
            default:{
                printf("opcion invalida\n");
                muestra=1;
                break;
            }
        }
    }
}


void ejecutarSYS(uint32_t operandoA){
    switch(operandoA){
        case SYS_READ:{
            readSYS(); // Leer de memoria
            break;
        }
        case SYS_WRITE:{
            writeSYS(); // Escribir en memoria
            break;
        }
        case SYS_STR_READ:{ //Lectura de string
            readSTR();
            printf("\n");
            break;
        }
        case SYS_STR_WRITE:{ //Escritura de string
            writeSTR();
            break;
        }
        case SYS_CLEAR:{
            system("cls||clear"); //cls es para windows
            break;
        }
        case SYS_BREAKPOINT:{
            if (archivo_vmi != NULL){
                guardarImagenVMI(archivo_vmi);
                breakPoint();
            }
            else
                printf("Breakpoint alcanzado, pero no se especificó archivo .vmi\n");
            break;
        }
        default:{
            detectaError(COD_ERR_OPE,0x0);
            return;
        }
    }
}
uint16_t convertirBigEndian16(uint16_t val) {
    return (val >> 8) | (val << 8);
}

uint32_t convertirBigEndian32(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}

//-------------------FUNCIONES DE EJECUCION-------------------------
int ejecutarInstruccion(){
    uint8_t posCS = Registros[POS_CS] >> 16;
    uint32_t offsetIP = Registros[POS_IP] & 0xFFFF;

    // Verificar si IP está dentro del code segment
    if (offsetIP >= tablaSegmentos[posCS].tamanio) {
        continuarEjecucion = 0;
        return 0;
    }

    uint32_t direccionFisica = calculaDireccionFisica(Registros[POS_IP]); // Calcular la direccion fisica

    if(direccionFisica > TAMANIO_MEMORIA){
        detectaError(COD_ERR_FIS,direccionFisica);
        return 1;
    }

    uint8_t codigo = leerMemoria(direccionFisica,1);
    uint8_t codOp = codigo & 0x1F;
    uint8_t cantOperandos = (codigo >> 4) & 0x01;
    uint8_t tipoA=OP_NING, tipoB =OP_NING, tamA=0,tamB=0;

    uint32_t operandoA=0, operandoB=0;
    // Decodifico el codigo de operacion y tipo de operandos

    Registros[POS_IP]++; // IP apunta a siguiente instruccion

    if((codOp!= OP_STOP) && (codOp!=OP_RET)){
        if(cantOperandos == 0x01){
            tipoB = (codigo >> 6) & 0x03;
            tipoA = (codigo >> 4) & 0x03;
            operandoB = obtenerOperando(tipoB, &Registros[POS_IP], &tamB);
            operandoA = obtenerOperando(tipoA, &Registros[POS_IP], &tamA);
        }else{
            tipoA = (codigo >> 6) & 0x03;
            operandoA = obtenerOperando(tipoA, &Registros[POS_IP], &tamA);
        }
    }

    // Ejecuta la instruccion
    switch(codOp){
        case OP_MOV:
            ejecutarMOV(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_ADD:
            ejecutarADD(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_SUB:
            ejecutarSUB(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_SWAP:
            ejecutarSWAP(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_MUL:
            ejecutarMUL(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_DIV:
            ejecutarDIV(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_CMP:
            ejecutarCMP(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_SHL:
            ejecutarSHL(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_SHR:
            ejecutarSHR(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_AND:
            ejecutarAND(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_OR:
            ejecutarOR(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_XOR:
            ejecutarXOR(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_LDL:
            ejecutarLDL(tipoA, operandoA,tipoB, operandoB, tamA, tamB);
            break;
        case OP_LDH:
            ejecutarLDH(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_RND:
            ejecutarRND(tipoA, operandoA, tipoB, operandoB, tamA, tamB);
            break;
        case OP_SYS:
            ejecutarSYS(operandoA);
            break;
        case OP_JMP:
            ejecutarJMP(tipoA, operandoA, tamA);
            break;
        case OP_JZ:
            ejecutarJZ(tipoA, operandoA, tamA);
            break;
        case OP_JP:
            ejecutarJP(tipoA, operandoA, tamA);
            break;
        case OP_JN:
            ejecutarJN(tipoA, operandoA, tamA);
            break;
        case OP_JNZ:
            ejecutarJNZ(tipoA, operandoA, tamA);
            break;
        case OP_JNP:
            ejecutarJNP(tipoA, operandoA, tamA);
            break;
        case OP_JNN:
            ejecutarJNN(tipoA, operandoA, tamA);
            break;
        case OP_NOT:{
            ejecutarNOT(tipoA, operandoA, tamA);
            break;
        }
        case OP_PUSH:{
            int32_t valor;
            if(tipoA== OP_MEM && operandoA == 0xFFFFFFFF){
                valor = operandoA;
            }
            else{
                valor = obtenerValorOperando(tipoA, operandoA, tamA);
            }
            ejecutarPUSH(valor);
            break;
        }
        case OP_POP:{
            int error=0;
            uint32_t valor = ejecutarPOP(&error);
            if(error==0){
                escribirValorOperando(tipoA, operandoA, valor, tamA);
            }

            break;
        }
        case OP_CALL:{
            uint32_t dirRedireccion = obtenerValorOperando(tipoA, operandoA,tamA);
            ejecutarCALL(dirRedireccion);
            break;
        }
        case OP_RET:{
            ejecutarRET();
            break;
        }
        case OP_STOP:{
            continuarEjecucion = 0; // Detener la ejecucion
            break;
        }
        default:{
            detectaError(COD_ERR_INS,codOp); // Error en la instruccion
            return 1;
        }
    }
    return 0;
}

int ejecutarPrograma () {
    while(continuarEjecucion ){
        if(ejecutarInstruccion()!=0)
            return 1;
    }
    return 0;
}

//---------------FUNCIONES PARA DISASSEMBLER---------------

// Funcion auxiliar para determinar tamanio del operando
int operandoSize(uint8_t tipo) {
    switch (tipo) {
        case OP_REG: return 1;
        case OP_INM: return 2;
        case OP_MEM: return 3;
        default: return 0;
    }
}

void decodificarOperando(uint32_t punt, int operandoSize,uint8_t codOp) {
uint8_t sector, numReg, codReg;
uint32_t valor;
int32_t offset;

switch (operandoSize) {
    case 1: { //Registro
        numReg = (MemoriaPrincipal[punt]>>4) & 0x0F;
        sector = (MemoriaPrincipal[punt] >> 2) & 0x03;
        switch (sector) {
            case 0x00: printf("%-s", NOMBRES_REGISTROS[numReg]);break;
            case 0x01: printf("%cL", NOMBRES_REGISTROS[numReg][1]);break; // Byte bajo (AL)
            case 0x02: printf("%cH", NOMBRES_REGISTROS[numReg][1]);break; // Byte alto (AH)
            case 0x03: printf("%cX", NOMBRES_REGISTROS[numReg][1]);break; // 16 bits (AX)
        }
        break;
    }
    case 2: { //Inmediato
        valor = (MemoriaPrincipal[punt] << 8) | MemoriaPrincipal[punt+1];
        if (valor & 0x8000) {
                valor |= 0xFFFF0000;
        }
        if(codOp==OP_JMP || codOp==OP_JN || codOp==OP_JNN || codOp==OP_JNP || codOp==OP_JNZ || codOp==OP_JP || codOp==OP_JZ){
             printf("%04X", valor);
        }else{
            printf("%-4d", (int32_t)valor);
        }
        break;
    }
    case 3: { //Memoria
        offset = (MemoriaPrincipal[punt] << 8) | MemoriaPrincipal[punt + 1];
        // Extender el signo para el offset
        if (offset & 0x8000) {
            offset |= 0xFFFF0000;
        }
        codReg = (MemoriaPrincipal[punt+2] >> 4) & 0x0F;

        if(versionPrograma==2){
            uint8_t modMem = MemoriaPrincipal[punt+2] & 0x03;
            switch(modMem){
                case MOD_BYTE: printf("b"); break;
                case MOD_WORD: printf("w"); break;
                case MOD_LONG: printf("l"); break;
            }
        }

        printf("[");
        if (codReg < NUM_REGISTROS && NOMBRES_REGISTROS[codReg][0] != '\0') {
            printf("%s", NOMBRES_REGISTROS[codReg]);
            if (offset != 0) {
                printf("%+d", offset);
            }
        }else{
            printf("%d", offset);
        }
        printf("]");
        break;
    }
    default: printf("?? ");break;
}
}


void disassemblerInstruccion(uint32_t *ip) {
    uint32_t ip0=(*ip)+1, offsetA, offsetB;
    uint8_t codigo = MemoriaPrincipal[*ip];
    uint8_t codOp = codigo & 0x1F;
    uint8_t tipoA = 0, tipoB = 0;
    char bytesStr[32] = ""; //Buffer para los bytes de la instruccion
    int bytesLen = 0;

    if(versionPrograma==1) {// Imprimir direccion
        printf("[%04X] ", *ip);
    } else if(versionPrograma==2){
        printf("[%04X] ", *ip - tablaSegmentos[Registros[POS_CS] >> 16].base);
    }

    //Bytes de la instruccion
    bytesLen += sprintf(bytesStr + bytesLen, "%02X", codigo);

    // Determinar formato de instruccion
    if(codOp == OP_STOP || codOp == OP_RET){ // Instruccion sin operandos
        printf("%-24s | %-6s", bytesStr, MNEMONICOS[codOp]);
        (*ip)++;
    }
    else if (codOp <= OP_CALL) {// Instruccion con 1 operando
        tipoA = (codigo >> 6) & 0x03;
        // Imprimir bytes
        for (int i = 0; i < operandoSize(tipoA); i++) {
            bytesLen += sprintf(bytesStr + bytesLen, " %02X", MemoriaPrincipal[(*ip)+1+i]);
        }
        printf("%-24s | %-6s ", bytesStr, MNEMONICOS[codOp]); // Imprimir operando A
        decodificarOperando(ip0, operandoSize(tipoA),codOp);
        (*ip) = operandoSize(tipoA)+ip0;
    }
    else {// Instruccion con 2 operandos
        tipoB = (codigo >> 6) & 0x03;
        tipoA = (codigo >> 4) & 0x03;

        //Agregar bytes de la instruccion B
        for (int i = 0; i < operandoSize(tipoB); i++) {
            bytesLen += sprintf(bytesStr + bytesLen," %02X", MemoriaPrincipal[(*ip) +1+i]);
        }
        //Agregar bytes de la instruccion A
        for (int i = 0; i < operandoSize(tipoA); i++) {
            bytesLen += sprintf(bytesStr + bytesLen," %02X", MemoriaPrincipal[(*ip) + operandoSize(tipoB) +1+i]);
        }
        printf("%-24s | %-6s", bytesStr, MNEMONICOS[codOp]);

        // Imprimir operandos
        offsetB = ip0;
        offsetA = ip0 + operandoSize(tipoB);
        decodificarOperando(offsetA, operandoSize(tipoA),codOp);
        printf(", ");
        decodificarOperando(offsetB, operandoSize(tipoB),codOp);
        (*ip) = offsetA + operandoSize(tipoA);
    }
    printf("\n");
}

void disassembleKS() {
    if (Registros[POS_KS] == 0xFFFFFFFF) return;

    uint8_t posKS = Registros[POS_KS]>>16;
    uint32_t base = tablaSegmentos[posKS].base;
    uint32_t tam = tablaSegmentos[posKS].tamanio;

    printf("Constant Segment (KS):\n");

    for (uint32_t i = 0; i < tam;) {
        uint32_t dir = base + i;
        char *str = (char*)&MemoriaPrincipal[dir];

        // Asegurar que haya un '\0' dentro del segmento
        int len = strnlen(str, tam - i);
        if (len == 0 || i + len >= tam) break;
        len++; // incluir el '\0'

        // Mostrar dirección física (4 dígitos hex)
        printf("[%04X] ", dir);

        // Mostrar hasta 6 bytes hex + ".." si hay más
        for (int j = 0; j < len && j < 6; j++) {
            printf("%02X ", (uint8_t)str[j]);
        }
        if (len > 6) printf("..");

        // Relleno si < 6 bytes (para alinear el '|')
        if (len <= 6) {
            for (int j = len; j < 6; j++) {
                printf("   ");
            }
        }

        // Mostrar cadena entre comillas con . si no imprimible
        printf(" | \"");
        for (int j = 0; j < len - 1; j++) {
            char c = str[j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        printf("\"\n");

        i += len;
    }
}

void disassembleProgramaMV1() {
    uint32_t ip = 0;
    uint32_t tamCod = tablaSegmentos[POS_CS].tamanio;

    printf("Maquina Virtual MV1 - Desensamblado\n");
    printf("Tamanio del codigo: %d bytes\n\n", tamCod);

    while (ip < tamCod) {
        disassemblerInstruccion(&ip);
    }
}

void disassembleProgramaMV2() {
    uint8_t posCode = Registros[POS_CS]>>16;
    uint32_t tamCode = tablaSegmentos[posCode].tamanio;
    uint32_t baseCode = tablaSegmentos[posCode].base;

    uint8_t posKS = Registros[POS_KS]>>16;
    uint32_t tam = tablaSegmentos[posKS].tamanio;

    // Mostrar información del header
    printf("Maquina Virtual MV2 - Desensamblado\n");
    printf("Code Segment: %d bytes\n", tamCode);
    printf("Const Segment: %d bytes\n\n", tam);

    // Desensamblar strings constantes (Const Segment)
    disassembleKS();

    // Desensamblar código (Code Segment)
    printf("\nCode Segment\n");
    uint32_t ip = baseCode;

    while (ip < baseCode + tamCode) {
        uint32_t offsetCS = ip - baseCode;
        printf("%c", offsetCS == entryPoint ? '>' : ' ');
        disassemblerInstruccion(&ip);
    }
}
void disassemblerPasoAPaso() {
    uint32_t ip = Registros[POS_IP] & 0xFFFF,baseCS = tablaSegmentos[Registros[POS_CS] >> 16].base,ipFis;
    ipFis=ip+baseCS;
    entryPoint=ipFis;
    disassemblerInstruccion(&ipFis);
}

void muestraDesensamblador(uint8_t version){
    switch(version){
        case 1:
            disassembleProgramaMV1();
            break;
        case 2:
            disassembleProgramaMV2();
            break;
        default:
            printf("Error: Version de desensamblador no soportada\n");
            break;
    }
}
