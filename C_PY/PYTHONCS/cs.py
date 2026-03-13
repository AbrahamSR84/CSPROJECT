import serial
import json

ser = serial.Serial('COM4', 115200)

lista_uid = []

print("Esperando tarjetas RFID...")

while True:
    try:
        linea = ser.readline().decode().strip()

        if linea:
            data = json.loads(linea)
            uid = data["uid"]

            print("Tarjeta RFID detectada:", uid)

            if uid not in lista_uid:
                lista_uid.append(uid)

                # guardar sin borrar el archivo
                with open("tarjetas.txt", "a") as archivo:
                    archivo.write(uid + "\n")

            print("Lista de tarjetas RFID:", lista_uid)

    except:
        pass