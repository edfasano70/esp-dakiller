# ESP8266 DaKiller

Temporizador acumulativo persistente para ESP8266 que controla un relé.

El dispositivo acumula el tiempo que lleva funcionando y, al alcanzar un tiempo
objetivo (3 horas por defecto, configurable desde la web hasta 99 días),
**activa un relé y detiene el conteo**. El tiempo acumulado, el objetivo y el
estado del relé se guardan en la EEPROM emulada en flash, por lo que sobreviven
a reinicios y cortes de alimentación.

Repositorio: <https://github.com/edfasano70/esp-dakiller>

## Características

- Conteo acumulativo basado en `millis()` (sin depender de un RTC).
- Persistencia en EEPROM con validación por *magic number* y checksum.
- Relé con estado persistente: si estaba activado al apagar, se restaura al arrancar.
- Reset por hardware para reiniciar el conteo.
- Filtro de rebotes (*debounce*) en el pin de reset.
- Polaridad del relé configurable (activo en alto o en bajo).
- Página web en modo punto de acceso (AP) para ver el estado y controlar el relé.
- Tiempo objetivo configurable desde la web, de 1 segundo a 99 días.
- LED interno: 1 destello por segundo en funcionamiento normal y una secuencia
  de 10 destellos rápidos + 5 s de pausa cuando se cumple el objetivo.

## Hardware / pines (ESP8266)

| Función      | Pin        | NodeMCU / D1 mini | Notas                                    |
|--------------|------------|-------------------|------------------------------------------|
| Relé         | GPIO 5     | D1                | Activo en ALTO por defecto (ver `RELE_ACTIVO_ALTO`) |
| Reset        | GPIO 14    | D5                | `INPUT_PULLUP`: conéctalo a GND para resetear |
| LED interno  | GPIO 2     | D4                | Activo en BAJO (`LED_BUILTIN`) |

> Se evitan GPIO0, 1, 2, 3, 15 y 16 para el relé porque tienen estados
> especiales durante el arranque. Si usas otro módulo, ajusta `RELE_PIN` y
> `RESET_PIN` a pines seguros.

### Conexión del relé

| Módulo de relé | Conexión                                      |
|----------------|-----------------------------------------------|
| `VCC`          | 5 V (VIN/USB) o 3V3 según el módulo           |
| `GND`          | GND del ESP8266                               |
| `IN`           | GPIO5 (D1)                                    |

> Al arrancar, algunos GPIO del ESP8266 quedan en HIGH brevemente. Si tu módulo
> de relé es activo en ALTO puede dar un pulso corto; los módulos activos en BAJO
> (lo más habitual) no se activan con ese pulso. Ajusta `RELE_ACTIVO_ALTO` según
> corresponda.

> Muchos módulos de relé de 5 V no conmutan de forma fiable con lógica de 3.3 V.
> Si es tu caso, usa un módulo optoacoplado o un transistor/etapa de nivel.

## Cómo funciona

1. Al arrancar, se recuperan de EEPROM el tiempo acumulado, el objetivo y el
   estado del relé.
2. Mientras el relé está **apagado**, el tiempo avanza y se muestra por el puerto
   serie cada segundo.
3. Cada 5 min se guarda el tiempo acumulado en EEPROM.
4. Al alcanzar el objetivo (3 horas por defecto, configurable hasta 99 días), el
   relé se **activa** y el conteo se **detiene** (queda fijado al valor objetivo).
5. Al pulsar el reset (GPIO 14 a GND), se reinicia el conteo a `00:00:00`, se apaga
   el relé y se borra el estado guardado.

## LED interno

El LED de la placa (GPIO2 / `LED_BUILTIN`) muestra el estado sin necesidad de
abrir la página web:

- **Funcionamiento normal:** un destello corto (100 ms) cada segundo.
- **Objetivo cumplido:** 10 destellos rápidos (100 ms encendido / 100 ms apagado),
  seguidos de 5 segundos apagado, repitiéndose en bucle.

El parpadeo es no bloqueante, así que no afecta al temporizador ni a la web.

## Página web

El ESP8266 crea su propia red WiFi:

| Dato       | Valor                                      |
|------------|--------------------------------------------|
| Red (SSID) | `DaKiller-XXXX` (XXXX = últimos 4 dígitos de la MAC) |
| Contraseña | `12345678`                                 |
| URL        | <http://192.168.4.1>                       |

Por ejemplo, para la MAC `BC:FF:4D:CF:E9:2B`, el SSID es `DaKiller-E92B`.

Pasos:

1. Conéctate desde el móvil o PC a la red WiFi **DaKiller-XXXX** (el SSID
   incluye los últimos 4 dígitos hexadecimales de la MAC en mayúsculas).
2. Abre `http://192.168.4.1` en el navegador.
3. Verás el tiempo transcurrido, el objetivo, el tiempo restante y el estado del
   relé, actualizándose cada segundo.
4. En la tarjeta **Cambiar tiempo objetivo** puedes indicar días (0-99), horas,
   minutos y segundos, y pulsar **Guardar objetivo**. El cambio se guarda en EEPROM.
5. Botones disponibles: **Encender relé**, **Apagar relé** y **Resetear conteo**.

> El objetivo máximo es de 99 días exactos. Si introduces `99` días junto con
> horas/minutos/segundos, se rechazará por superar el límite.
>
> Al cambiar el objetivo no se reinicia el tiempo transcurrido. Si el relé estaba
> activado porque se había cumplido el objetivo anterior y el nuevo objetivo es
> mayor, se apaga y el conteo se reanuda automáticamente.

> Cuando el tiempo objetivo se ha cumplido, el botón de apagar se deshabilita:
> apagar el relé lo reactivaría al instante. Usa **Resetear conteo** para volver
> a empezar.

Endpoints HTTP (por si quieres automatizarlos):

| Método | Ruta                          | Acción                             |
|--------|-------------------------------|------------------------------------|
| GET    | `/`                           | Página web                         |
| GET    | `/estado`                     | Estado en JSON                     |
| POST   | `/reset`                      | Reinicia el conteo                 |
| POST   | `/rele?estado=on`             | Enciende el relé (manual)          |
| POST   | `/rele?estado=off`            | Apaga el relé (si no terminó)      |
| POST   | `/objetivo?d=0&h=3&m=0&s=0`   | Cambia el objetivo (días/h/m/s)    |

La red deja de emitir si el ESP8266 se queda sin alimentación; al volver a
encenderla, el AP se levanta de nuevo.

## Configuración

Los parámetros principales están al inicio de
`dakiller-esp8266/dakiller-esp8266.ino`:

```cpp
const int RELE_PIN = 5;                     // GPIO del relé
const int RESET_PIN = 14;                   // GPIO de reset
const uint64_t TIEMPO_OBJETIVO_POR_DEFECTO_MS = 3ULL*60ULL*60ULL*1000ULL; // 3 h
const uint64_t TIEMPO_MAX_OBJETIVO_MS = 99ULL*24ULL*60ULL*60ULL*1000ULL;  // 99 días
const unsigned long TIEMPO_REBOTE_MS = 50;  // Debounce del reset
const unsigned long INTERVALO_GUARDADO_MS = 5UL*60UL*1000UL; // Guardado en EEPROM
const bool RELE_ACTIVO_ALTO = true;         // false si el relé es activo en BAJO

const char *AP_SSID_BASE = "DaKiller";      // Prefijo del SSID
const char *AP_PASS = "12345678";           // Mínimo 8 caracteres
// El SSID final se genera en setup como "DaKiller-XXXX" a partir de la MAC.
```

> El SSID se genera automáticamente en cada arranque con los últimos 4 dígitos
> hexadecimales de la MAC del chip.

> El objetivo se puede cambiar en caliente desde la web; no hace falta recompilar.

## Instalación

### Requisitos

- Placa ESP8266 (NodeMCU, Wemos D1 mini o similar).
- [arduino-cli](https://arduino.github.io/arduino-cli/) o Arduino IDE 2.x con el
  core `esp8266:esp8266`.
- Módulo de relé y cables.

### Clonar el repositorio

```bash
git clone https://github.com/edfasano70/esp-dakiller.git
cd esp-dakiller
```

### Compilar y subir con arduino-cli

```bash
# 1. Instalar el core (si no lo tienes)
arduino-cli core install esp8266:esp8266

# 2. Compilar el sketch (está en la subcarpeta dakiller-esp8266)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 dakiller-esp8266

# 3. Subir (ajusta el puerto si es necesario)
arduino-cli upload -p /dev/ttyUSB0 \
  --fqbn esp8266:esp8266:nodemcuv2:baud=57600 dakiller-esp8266

# 4. Ver la salida serie
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

> Arduino exige que el archivo `.ino` y su carpeta se llamen igual. Por eso el
> sketch vive en `dakiller-esp8266/dakiller-esp8266.ino`; si renombras uno,
> renombra también el otro.

> Si tu placa es una NodeMCU o Wemos D1 mini, `nodemcuv2` es un buen punto de
> partida. Para placas genéricas usa `esp8266:esp8266:generic`. Si la subida falla
> con un CH340, prueba a `57600` baudios como en el ejemplo.

### Abrir con Arduino IDE

Abre `dakiller-esp8266/dakiller-esp8266.ino` con Arduino IDE, selecciona tu placa
ESP8266 y el puerto, y pulsa **Subir**.

## Estructura del repositorio

```text
esp-dakiller/
├── README.md
└── dakiller-esp8266/
    └── dakiller-esp8266.ino
```

## Salida serie de ejemplo

```text
Tiempo objetivo: 03:00:00
Tiempo recuperado de EEPROM: 00:00:00
Rele inactivo. El conteo está en curso.
Tiempo actual transcurrido: 00:00:05
...
Guardando tiempo: 00:05:00
...
!!! TIEMPO CUMPLIDO (03:00:00) - RELE ACTIVADO Y CONTEO DETENIDO !!!
```

Ejemplo con un objetivo de varios días:

```text
Tiempo objetivo: 10d 00:00:00
Tiempo actual transcurrido: 9d 23:59:59
Tiempo actual transcurrido: 10d 00:00:00
```

## Solución de problemas

- **No aparece la red `DaKiller-XXXX`:** espera unos segundos tras encender,
  comprueba la alimentación y que el firmware se haya subido correctamente.
- **No puedo subir el sketch:** selecciona la placa correcta, instala el driver
  del CH340 y prueba a `57600` baudios.
- **La página web no carga:** asegúrate de estar conectado a la red del ESP8266
  (no a tu WiFi habitual) y abre `http://192.168.4.1`.
- **El relé se activa al arrancar:** probablemente tu módulo es activo en ALTO y
  recibe un pulso durante el boot. Usa un módulo activo en BAJO o revisa
  `RELE_ACTIVO_ALTO`.
- **Se perdió el conteo tras actualizar el firmware:** si cambia la versión del
  formato de EEPROM, el estado anterior se descarta y se inicializa de nuevo.

## Advertencias

- Verifica la polaridad de tu módulo de relé (`RELE_ACTIVO_ALTO`). Un valor
  incorrecto puede activar el relé al revés de lo esperado.
- La EEPROM del ESP8266 se emula sobre un sector de flash con vida finita. Por
  eso el guardado se hace cada 5 min. En un corte de alimentación se pierde,
  como máximo, el tiempo transcurrido desde el último guardado (≤ 5 min).
- Al cambiar el objetivo, el tiempo acumulado no se reinicia. Si quieres empezar
  de cero con el nuevo objetivo, pulsa **Resetear conteo**.
- El guardado valida un checksum; si el contenido está corrupto, el dispositivo
  arranca desde cero.
- Cambia `AP_PASS` por una contraseña propia. La red `DaKiller-XXXX` es local y
  no tiene acceso a Internet.
