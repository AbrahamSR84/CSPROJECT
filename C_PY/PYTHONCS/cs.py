# Código Python para comunicación con ESP32 a través de puerto serial
# Librerias
import time
import serial
import json

ser = serial.Serial('COM4', 115200) # Declarar el puerto que se esta utilizando
time.sleep(2)  # esperar inicio ESP32

lista_uid = []

# Solicitar lista inicial de tarjetas al ESP32
ser.write(b"LIST\n")

print("Esperando tarjetas RFID...")
# Bucle principal para leer datos del ESP32
while True:
    try: 
        linea = ser.readline().decode(errors="ignore").strip() # Leer línea del puerto serial y decodificarla
        if not linea:
            continue

        print("ESP32:", linea)

        try:
            data = json.loads(linea) # Intentar decodificar la línea como JSON

            if isinstance(data, dict) and "uid" in data: # Nueva tarjeta recibida del ESP32
                uid = data["uid"]
                if uid not in lista_uid: # Verificar si el UID ya está en la lista
                    lista_uid.append(uid)
                    with open("tarjetas.txt", "a") as f: # Abrir el archivo en modo append para agregar el nuevo UID
                        f.write(uid + "\n") # Escribir el nuevo UID en el archivo
                    mensaje = json.dumps(lista_uid, separators=(',', ':')) # Convertir la lista de UIDs a JSON sin espacios
                    ser.write((mensaje + "\n").encode()) 
                    print("JSON enviado al ESP32:", mensaje)

            elif isinstance(data, list):
                # Lista completa recibida del ESP32
                lista_uid = data
                print("Lista completa actualizada:", lista_uid)

        except json.JSONDecodeError: # Si la línea no es un JSON válido, simplemente se ignora
            pass

    except Exception as e: # Capturar cualquier otra excepción y mostrar el error
        print("Error:", e)