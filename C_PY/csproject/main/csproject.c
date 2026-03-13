/* Código ESP32 para manejo de tarjetas RFID y comunicación con Python */
/* Librerías */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
/* Pines */
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_RST 22
/* SPI y UART */
#define SPI_HOST SPI2_HOST
#define UART_PORT UART_NUM_0
#define BUF_SIZE 128
#define MAX_TARJETAS 50

static spi_device_handle_t spi; /*Manejador del dispositivo SPI para el RC522*/

/* Modos de operación */
typedef enum
{
    MODO_NORMAL,
    MODO_ADD,
    MODO_DEL
} modo_t;
static modo_t modo = MODO_NORMAL;

/* Almacenamiento de tarjetas */
static uint8_t tarjetas[MAX_TARJETAS][5];
static int total_tarjetas = 0;

/* Funciones SPI para RC522 */
void rc522_write(uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {0};
    uint8_t data[2] = {(reg << 1) & 0x7E, value};
    t.length = 16;
    t.tx_buffer = data;
    spi_device_transmit(spi, &t);
}

uint8_t rc522_read(uint8_t reg)
{
    spi_transaction_t t = {0};
    uint8_t tx[2] = {((reg << 1) & 0x7E) | 0x80, 0x00}; /*El bit de lectura se establece en 1*/
    uint8_t rx[2];
    t.length = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_transmit(spi, &t);
    return rx[1];
}

void rc522_set_bitmask(uint8_t reg, uint8_t mask) { rc522_write(reg, rc522_read(reg) | mask); }
void rc522_clear_bitmask(uint8_t reg, uint8_t mask) { rc522_write(reg, rc522_read(reg) & ~mask); }
/*Reset del RC522*/
void rc522_reset()
{
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}
/*Inicialización del RC522*/
void rc522_init()
{
    rc522_reset();
    rc522_write(0x01, 0x0F);
    vTaskDelay(pdMS_TO_TICKS(50));
    rc522_write(0x2A, 0x8D);
    rc522_write(0x2B, 0x3E);
    rc522_write(0x2D, 30);
    rc522_write(0x2C, 0);
    rc522_write(0x15, 0x40);
    rc522_write(0x11, 0x3D);
    rc522_set_bitmask(0x14, 0x03);
}

/*Funciones para manejar tarjetas*/
bool rc522_request() {
    rc522_write(0x01, 0x00);
    rc522_write(0x02, 0x77 | 0x80);
    rc522_write(0x04, 0x7F);
    rc522_set_bitmask(0x0A, 0x80);
    rc522_write(0x0D, 0x07);
    rc522_write(0x09, 0x26);
    rc522_write(0x01, 0x0C);
    rc522_set_bitmask(0x0D, 0x80);

    int i = 2000;
    while (i-- && !(rc522_read(0x04) & 0x30));
    rc522_clear_bitmask(0x0D, 0x80);

    if (i <= 0) return false;
    if (rc522_read(0x06) & 0x1B) return false;
    if (rc522_read(0x0A) != 2) return false;
    return true;
}
/*Función para obtener el UID de una tarjeta*/
bool rc522_anticoll(uint8_t *uid) { 
    rc522_write(0x01, 0x00);
    rc522_write(0x02, 0x77 | 0x80);
    rc522_write(0x04, 0x7F);
    rc522_set_bitmask(0x0A, 0x80);
    rc522_write(0x0D, 0x00);
    rc522_write(0x09, 0x93);
    rc522_write(0x09, 0x20);
    rc522_write(0x01, 0x0C);
    rc522_set_bitmask(0x0D, 0x80);

    /* Esperar a que se complete la lectura */
    int i = 2000;
    while (i-- && !(rc522_read(0x04) & 0x30));
    rc522_clear_bitmask(0x0D, 0x80);
    /**/
    if (i <= 0) return false;
    if (rc522_read(0x06) & 0x1B) return false;
    if (rc522_read(0x0A) != 5) return false;

    for (int j = 0; j < 5; j++)
        uid[j] = rc522_read(0x09);

    return true;
}
bool tarjeta_existe(uint8_t *uid)
{
    for (int i = 0; i < total_tarjetas; i++) /*Recorrer la lista de tarjetas*/
        if (memcmp(tarjetas[i], uid, 4) == 0)
            return true;
    return false;
}
void agregar_tarjeta(uint8_t *uid) /*Función para agregar una nueva tarjeta*/
{
    if (total_tarjetas >= MAX_TARJETAS || tarjeta_existe(uid))
        return;
    memcpy(tarjetas[total_tarjetas], uid, 4);
    total_tarjetas++;
}
void eliminar_tarjeta(uint8_t *uid) /*Función para eliminar una tarjeta existente*/
{
    for (int i = 0; i < total_tarjetas; i++)
    {
        if (memcmp(tarjetas[i], uid, 4) == 0) /*Si se encuentra la tarjeta*/
        {
            for (int j = i; j < total_tarjetas - 1; j++)
                memcpy(tarjetas[j], tarjetas[j + 1], 4);
            total_tarjetas--;
            return;
        }
    }
}

/* Conversión de hexadecimal a bytes */
void hexStringToBytes(char *hex, uint8_t *bytes)
{
    for (int i = 0; i < 4; i++) 
        sscanf(hex + 2 * i, "%2hhx", &bytes[i]); /*Lee 2 caracteres hexadecimales y los convierte a un byte*/
}

/* Tarea RFID */
void rfid_task(void *arg)
{
    uint8_t uid[5];
    while (1)
    {
        if (rc522_request() && rc522_anticoll(uid)) /*Si se detecta una tarjeta*/
        {
            char uid_str[11] = ""; /*Array para almacenar el UID en formato hexadecimal*/
            for (int i = 0; i < 4; i++)
                sprintf(uid_str + i * 2, "%02X", uid[i]); /*Convertir cada byte del UID a su representación hexadecimal y concatenarla en uid_str*/
            printf("{\"uid\":\"%s\"}\n", uid_str);
            vTaskDelay(pdMS_TO_TICKS(1000)); /*Sirve para evitar lecturas múltiples de la misma tarjeta */
        }
        vTaskDelay(pdMS_TO_TICKS(200)); /*Pequeña espera para reducir carga de la CPU*/
    }
}

/*Tarea consola */
void consola_task(void *arg)
{
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uint8_t data[BUF_SIZE];
    /* Procesar comandos recibidos por UART */
    while (1)
    {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len <= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(50)); 
            continue;
        }

        data[len] = '\0';
        char *cmd = (char *)data;
        cmd[strcspn(cmd, "\r\n")] = 0;

        if (cmd[0] == '{')
            continue; /*Ignora el {} JSON RFID de la cadena*/
        if (cmd[0] == '[')
        { /*Recibir la lista desde Python*/
            char *token = strtok(cmd, "[\",]");
            while (token != NULL)
            {
                if (strlen(token) >= 8) /*Si el token tiene al menos 8 caracteres (4 bytes en hexadecimal) */
                {
                    uint8_t uid_bytes[4]; /*Array para almacenar los bytes del UID */
                    hexStringToBytes(token, uid_bytes);
                    agregar_tarjeta(uid_bytes);
                }
                token = strtok(NULL, "[\",]"); /*Obtener el siguiente token*/
            }
            continue;
        }

        if (strcmp(cmd, "ADD") == 0) /*Agregar tarjeta*/
        {
            modo = MODO_ADD;
        }
        else if (strcmp(cmd, "DEL") == 0) /*Eliminar tarjeta*/
        {
            modo = MODO_DEL;
        }
        else if (strcmp(cmd, "LIST") == 0) /*Listar tarjetas*/
        { /*List en JSON limpio*/
            printf("[");
            for (int i = 0; i < total_tarjetas; i++) /*Recorrer la lista de tarjetas*/
            {
                printf("\"%02X%02X%02X%02X\"", tarjetas[i][0], tarjetas[i][1], tarjetas[i][2], tarjetas[i][3]);
                if (i < total_tarjetas - 1)
                    printf(",");
            }
            printf("]\n");
        }
    }
}

/*Main*/
void app_main(void)
{
    spi_bus_config_t buscfg = {.miso_io_num = PIN_NUM_MISO, .mosi_io_num = PIN_NUM_MOSI, .sclk_io_num = PIN_NUM_CLK, .quadwp_io_num = -1, .quadhd_io_num = -1};
    spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO); 

    spi_device_interface_config_t devcfg = {.clock_speed_hz = 1000000, .mode = 0, .spics_io_num = PIN_NUM_CS, .queue_size = 1};
    spi_bus_add_device(SPI_HOST, &devcfg, &spi);

    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 1);

    rc522_init();
    xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 5, NULL);
    xTaskCreate(consola_task, "consola_task", 4096, NULL, 5, NULL);
}