/*Código de sp32*/
/*Librerías*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
/* Definición de pines*/
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define PIN_NUM_RST  22
/* Definición de host SPI*/
#define SPI_HOST SPI2_HOST
#define MAX_TARJETAS 50
#define UART_PORT UART_NUM_0
#define BUF_SIZE 128
/*Manejo de SPI*/
static spi_device_handle_t spi;

/*Tipos de modo*/
typedef enum {
    MODO_NORMAL,
    MODO_ADD,
    MODO_DEL
} modo_t;

static modo_t modo = MODO_NORMAL;
/*Almacenamiento de tarjetas autorizadas*/
static uint8_t tarjetas[MAX_TARJETAS][4]; /*Array para almacenar las tarjetas autorizadas */
static int total_tarjetas = 0; /*Contador de tarjetas autorizadas */

/*Funciones para interactuar con el RC522 a través de SPI */ 
void rc522_write(uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {0};
    uint8_t data[2];
    data[0] = (reg << 1) & 0x7E;
    data[1] = value;
    t.length = 16;
    t.tx_buffer = data;
    spi_device_transmit(spi, &t);
}
/*Función para leer un registro del RC522*/
uint8_t rc522_read(uint8_t reg)
{
    /* Para leer, el bit 7 del primer byte debe ser 1 */
    spi_transaction_t t = {0};
    uint8_t tx[2];
    uint8_t rx[2];
    /*La lectura se realiza enviando un 1 en el bit 7 del primer byte */
    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;

    t.length = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    /*Transmitir la transacción y leer el resultado*/
    spi_device_transmit(spi, &t);
    return rx[1];
}
/*Función para establecer un bit del RC522*/
void rc522_set_bitmask(uint8_t reg, uint8_t mask)
{
    rc522_write(reg, rc522_read(reg) | mask);
}
/*Función para borrar un bit del RC522*/
void rc522_clear_bitmask(uint8_t reg, uint8_t mask)
{
    rc522_write(reg, rc522_read(reg) & (~mask));
}

void rc522_reset()
{
    /*Resetear el RC522*/
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void rc522_init()
{
    /*Inicializar el RC522*/
    rc522_reset();
    rc522_write(0x01, 0x0F);
    vTaskDelay(pdMS_TO_TICKS(50));
    /*Configurar los registros del RC522*/
    rc522_write(0x2A, 0x8D);
    rc522_write(0x2B, 0x3E);
    rc522_write(0x2D, 30);
    rc522_write(0x2C, 0);
    rc522_write(0x15, 0x40);
    rc522_write(0x11, 0x3D);
    rc522_set_bitmask(0x14, 0x03);
}
/*Función para solicitar una tarjeta RFID*/
bool rc522_request()
{
    rc522_write(0x01, 0x00);
    rc522_write(0x02, 0x77 | 0x80);
    rc522_write(0x04, 0x7F);
    rc522_set_bitmask(0x0A, 0x80);
    /*Configurar el tiempo de espera*/
    rc522_write(0x0D, 0x07);
    rc522_write(0x09, 0x26);
    rc522_write(0x01, 0x0C);
    rc522_set_bitmask(0x0D, 0x80);
    /*Esperar a que se encuentre una tarjeta*/
    int i = 2000;
    while (i-- && !(rc522_read(0x04) & 0x30)); 

    rc522_clear_bitmask(0x0D, 0x80);
    /*Verificar si se encontró una tarjeta*/
    if (i <= 0) return false;
    if (rc522_read(0x06) & 0x1B) return false;
    if (rc522_read(0x0A) != 2) return false;
    /*Si se encontró una tarjeta, retornar true*/
    return true;
}
/*Función para evitar colisiones con múltiples tarjetas RFID*/
bool rc522_anticoll(uint8_t *uid)
{
    rc522_write(0x01, 0x00);
    rc522_write(0x02, 0x77 | 0x80);
    rc522_write(0x04, 0x7F);
    rc522_set_bitmask(0x0A, 0x80);
    /*Configurar el tiempo de espera*/
    rc522_write(0x0D, 0x00);
    rc522_write(0x09, 0x93);
    rc522_write(0x09, 0x20);
    rc522_write(0x01, 0x0C);
    rc522_set_bitmask(0x0D, 0x80);
    /*Esperar a que se encuentre una tarjeta*/
    int i = 2000;
    while (i-- && !(rc522_read(0x04) & 0x30));
    /*Limpiar el bit de espera*/
    rc522_clear_bitmask(0x0D, 0x80);
    /*Verificar si se encontró una tarjeta y leer su UID*/
    if (i <= 0) return false;
    if (rc522_read(0x06) & 0x1B) return false;
    if (rc522_read(0x0A) != 5) return false;
    /*Leer el UID de la tarjeta*/
    for (int j = 0; j < 5; j++)
        uid[j] = rc522_read(0x09);
    /*Si se encontró una tarjeta, retornar true*/
    return true;
}

/*Función para verificar si una tarjeta ya está registrada*/
bool tarjeta_existe(uint8_t *uid)
{   
    for (int i = 0; i < total_tarjetas; i++)/*Recorrer todas las tarjetas registradas*/
        if (memcmp(tarjetas[i], uid, 4) == 0)  /*Si se encuentra la tarjeta, retornar true*/
            return true;
    return false;
}

void agregar_tarjeta(uint8_t *uid) /*Función para agregar una tarjeta*/
{
    if (total_tarjetas >= MAX_TARJETAS) return;
    if (tarjeta_existe(uid)) return; /*Si la tarjeta ya existe, no agregarla*/

    memcpy(tarjetas[total_tarjetas], uid, 4); /*Copiar el UID de la tarjeta al array de tarjetas*/
    total_tarjetas++; /*Incrementar el contador de tarjetas*/
    printf("Tarjeta agregada\n");
}

void eliminar_tarjeta(uint8_t *uid) /*Función para eliminar una tarjeta*/
{
    for (int i = 0; i < total_tarjetas; i++) {
        if (memcmp(tarjetas[i], uid, 4) == 0) { /*Si se encuentra la tarjeta, eliminarla*/
            for (int j = i; j < total_tarjetas - 1; j++) 
                memcpy(tarjetas[j], tarjetas[j+1], 4);/* Desplazar las tarjetas */

            total_tarjetas--; /*Reducir el contador de tarjetas*/
            printf("Tarjeta eliminada\n");
            return; /*Salir de la función*/
        }
    }
}

/*RFID*/
void rfid_task(void *arg) /*Función para la tarea de RFID*/
{
    uint8_t uid[5];

    while (1) { /*Bucle infinito*/
        /*Si se detecta una tarjeta y se obtiene su UID*/
        if (rc522_request() && rc522_anticoll(uid)) {

            char uid_str[50] = ""; /*String para almacenar el UID en formato hexadecimal*/
            char temp[5];

            for (int i = 0; i < 5; i++) {
                sprintf(temp, "%02X", uid[i]); /*Formatear el UID en hexadecimal */
                strcat(uid_str, temp);
            }

            printf("{\"uid\":\"%s\"}\n", uid_str);  /*Imprimir el UID de la tarjeta en formato JSON*/

            vTaskDelay(pdMS_TO_TICKS(1000)); /*Esperar un segundo para evitar lecturas repetidas*/
        }

        vTaskDelay(pdMS_TO_TICKS(200)); /*Pequeña espera para evitar lecturas excesivas*/
    }
}

/*CONSOLA*/
void consola_task(void *arg) /*Función para la tarea de consola*/
{
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0); /*Instalar el driver UART */

    uint8_t data[BUF_SIZE];

    while (1) /*Bucle infinito*/
    {
        int len = uart_read_bytes(UART_PORT, /*Leer datos del puerto serie */
                                  data,
                                  BUF_SIZE - 1,
                                  pdMS_TO_TICKS(100));

        if (len > 0) /*Si se recibieron datos */
        {
            data[len] = '\0'; /*Null-terminate the string*/

            char *cmd = (char*)data;/*Apuntador a la cadena de caracteres recibida*/

            cmd[strcspn(cmd, "\r\n")] = 0; /*Eliminar caracteres de nueva línea*/

            for (int i = 0; cmd[i]; i++)
                cmd[i] = toupper((unsigned char)cmd[i]); /*Convertir el comando a mayúsculas*/

            if (strcmp(cmd, "ADD") == 0) /*Si se recibe el comando ADD*/
            {
                modo = MODO_ADD; /*Establecer el modo a agregar*/
                printf("Modo agregar activado\n");
            }
            else if (strcmp(cmd, "DEL") == 0) /*Si se recibe el comando DEL*/
            {
                modo = MODO_DEL; /*Establecer el modo a eliminar*/
                printf("Modo eliminar activado\n"); /*Imprimir mensaje de modo eliminar activado*/
            }
            else if (strcmp(cmd, "LIST") == 0)/*Si se recibe el comando LIST*/
            {
                printf("Total tarjetas: %d\n", total_tarjetas); /*Imprimir el total de tarjetas registradas*/
            }
            else
            {
                printf("Comando invalido\n"); /*Imprimir mensaje de comando inválido*/
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); /*Pequeña espera para evitar lecturas excesivas*/
    }
}

/*MAIN*/
void app_main(void) /*Función principal*/
{
    /* Configurar el bus SPI */
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    /*Configurar el dispositivo SPI y el bus de datos*/
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, 
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    spi_bus_add_device(SPI_HOST, &devcfg, &spi); /*Agregar el dispositivo SPI*/

    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT); /*Establecer la dirección del pin RST como salida*/
    gpio_set_level(PIN_NUM_RST, 1);

    rc522_init(); /*Inicializar el módulo RC522*/

    xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 5, NULL); /*Crear la tarea de RFID*/
    xTaskCreate(consola_task, "consola_task", 4096, NULL, 5, NULL); /*Crear la tarea de consola*/
}