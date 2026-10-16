# ESP32 DaKiller

Temporizador acumulativo persistente para ESP32 que controla un relé.

El dispositivo acumula el tiempo que lleva funcionando y, al alcanzar un tiempo
objetivo (3 horas por defecto), **activa un relé y detiene el conteo**. El tiempo
acumulado y el estado del relé se guardan en la memoria no volátil (NVS), por lo
que sobreviven a reinicios y cortes de alimentación.

## Características

- Conteo acumulativo basado en `millis()` (sin depender de un RTC).
- Persistencia en NVS mediante `Preferences` (guardado cada 10 s).
- Relé con estado persistente: si estaba activado al apagar, se restaura al arrancar.
- Reset por hardware para reiniciar el conteo.
- Filtro de rebotes (*debounce*) en el pin de reset.
- Polaridad del relé configurable (activo en alto o en bajo).

## Hardware / pines

| Función      | Pin        | Notas                                    |
|--------------|------------|------------------------------------------|
| Relé         | GPIO 27    | Activo en ALTO por defecto (ver `RELE_ACTIVO_ALTO`) |
| Reset        | GPIO 14    | `INPUT_PULLUP`: conéctalo a GND para resetear |

## Cómo funciona

1. Al arrancar, se recuperan de NVS el tiempo acumulado y el estado del relé.
2. Mientras el relé está **apagado**, el tiempo avanza y se muestra por el puerto
   serie cada segundo.
3. Cada 10 s se guarda el tiempo acumulado en NVS.
4. Al alcanzar `TIEMPO_OBJETIVO_MS` (3 horas), el relé se **activa** y el conteo
   se **detiene** (queda fijado al valor objetivo).
5. Al pulsar el reset (GPIO 14 a GND), se reinicia el conteo a `00:00:00`, se apaga
   el relé y se borra el estado guardado.

## Configuración

Los parámetros principales están al inicio de `esp32_dakiller.ino`:

```cpp
const int RELE_PIN = 27;                    // Pin del relé
const int RESET_PIN = 14;                   // Pin de reset
const long TIEMPO_OBJETIVO_MS = 3L*60L*60L*1000L; // 3 horas
const unsigned long TIEMPO_REBOTE_MS = 50;  // Debounce del reset
const bool RELE_ACTIVO_ALTO = true;         // false si el relé es activo en BAJO
```

## Compilar y subir con arduino-cli

Requisitos: [arduino-cli](https://arduino.github.io/arduino-cli/) y el core de ESP32.

```bash
# 1. Añadir el índice de placas de Espressif (si no lo tienes)
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json

# 2. Actualizar el índice e instalar el core
arduino-cli core update-index
arduino-cli core install esp32:esp32

# 3. Compilar
arduino-cli compile --fqbn esp32:esp32:esp32 .

# 4. Subir (ajusta el puerto si es necesario)
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .

# 5. Ver la salida serie
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

> Nota: si tu placa es un modelo concreto (S2, S3, C3, etc.), usa el FQBN
> correspondiente, p. ej. `esp32:esp32:esp32s3`.

## Salida serie de ejemplo

```
Tiempo objetivo: 03:00:00
Tiempo recuperado de NVS: 00:00:00
Rele inactivo. El conteo está en curso.
Tiempo actual transcurrido: 00:00:05
...
Guardando tiempo: 00:00:10
...
!!! TIEMPO CUMPLIDO (03:00:00) - RELE ACTIVADO Y CONTEO DETENIDO !!!
```

## Advertencias

- Verifica la polaridad de tu módulo de relé (`RELE_ACTIVO_ALTO`). Un valor
  incorrecto puede activar el relé al revés de lo esperado.
- En un corte de alimentación se pierde, como máximo, el tiempo transcurrido
  desde el último guardado (≤ 10 s).
