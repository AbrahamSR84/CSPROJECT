# Librería para comunicarse por puerto serial con el ESP32
import serial
# Librería para interpretar datos en formato JSON
import json
# Conexión al puerto donde está el ESP32 (velocidad 115200)
ser = serial.Serial('COM4', 115200)
# Mensaje inicial en consola
print("Esperando tarjetas RFID...")
# Bucle infinito para leer continuamente el puerto serial
while True:
    try:
        # Lee una línea enviada por el ESP32
        linea = ser.readline().decode().strip()

        # Verifica que se haya recibido información
        if linea:
            # Convierte el JSON recibido a un diccionario
            data = json.loads(linea)

            # Obtiene el UID de la tarjeta
            uid = data["uid"]

            # Muestra el UID en consola
            print("Tarjeta RFID detectada:", uid)

    except:
        # Ignora errores y sigue ejecutando
        pass