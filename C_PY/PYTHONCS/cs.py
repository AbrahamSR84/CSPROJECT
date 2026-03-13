import serial
import json

# Conexión con el ESP32 (cambia COM4 si tu puerto es diferente)
ser = serial.Serial('COM4', 115200)

# Lista donde se guardarán los UID leídos
lista_uid = []

print("Esperando tarjetas RFID...")

while True:
    try:
        # Leer datos enviados por el ESP32
        linea = ser.readline().decode().strip()

        if linea:
            # Convertir el JSON recibido a diccionario
            data = json.loads(linea)

            # Obtener UID de la tarjeta
            uid = data["uid"]

            print("Tarjeta detectada:", uid)

            # Verificar que el UID no esté ya en la lista
            if uid not in lista_uid:
                lista_uid.append(uid)

                # Guardar la lista en un archivo
                with open("tarjetas.txt", "w") as archivo:
                    for tarjeta in lista_uid:
                        archivo.write(tarjeta + "\n")

            # Mostrar lista actual
            print("Lista de tarjetas:", lista_uid)

    except:
        # Ignorar errores y continuar
        pass