#include <EEPROM.h>
#include <stddef.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// Definiciones de pines (ESP8266 / NodeMCU)
// Evitamos GPIO0, 1, 2, 3, 15 y 16 porque tienen estados especiales al arrancar.
const int RELE_PIN = 5;   // D1 en NodeMCU / Wemos D1 mini
const int RESET_PIN = 14; // D5, INPUT_PULLUP: conéctalo a GND para resetear

// LED interno de la placa (GPIO2 en NodeMCU / Wemos D1 mini).
// En estas placas el LED está conectado a 3V3, por lo que se enciende en BAJO.
const int LED_PIN = LED_BUILTIN;
const bool LED_ACTIVO_ALTO = false;

// Tiempo objetivo por defecto: 3 horas. Se puede cambiar desde la web.
const uint64_t TIEMPO_OBJETIVO_POR_DEFECTO_MS = 3ULL * 60ULL * 60ULL * 1000ULL;
const uint64_t TIEMPO_MAX_OBJETIVO_MS = 99ULL * 24ULL * 60ULL * 60ULL * 1000ULL;

// Duración mínima de una pulsación para considerarla válida (filtro de rebotes)
const unsigned long TIEMPO_REBOTE_MS = 50;

// Intervalo de guardado en EEPROM.
// La EEPROM del ESP8266 se implementa sobre un sector de flash con vida finita.
// 5 minutos permite soportar objetivos de hasta 99 días sin agotar la flash.
const unsigned long INTERVALO_GUARDADO_MS = 5UL * 60UL * 1000UL;

// Nivel lógico con el que el relé se ACTIVA.
// - true  -> relé activo en ALTO (HIGH = activado)
// - false -> relé activo en BAJO  (LOW  = activado, la mayoría de módulos)
const bool RELE_ACTIVO_ALTO = true;

// --- Punto de acceso WiFi ---
const char *AP_SSID_BASE = "DaKiller";
const char *AP_PASS = "12345678"; // mínimo 8 caracteres
char AP_SSID[24];                 // se genera en setup: "DaKiller-XXXX"

// --- Persistencia en EEPROM ---
const uint32_t MAGIC = 0x44414B32; // "DAK2"
const uint8_t VERSION = 2;

struct DatosPersistentes {
  uint32_t magic;
  uint64_t objetivo_ms;
  uint64_t tiempo_ms;
  uint32_t checksum;
  uint8_t rele_activado;
  uint8_t version;
};

// Variables de estado
uint64_t objetivo_ms = TIEMPO_OBJETIVO_POR_DEFECTO_MS;
uint64_t tiempo_transcurrido_total_ms = 0; // Tiempo total que ha estado encendido
unsigned long tiempo_inicio_ciclo = 0;     // Valor de millis() al inicio del ciclo actual
bool rele_activado = false;                // Estado actual del relé (se inicializa desde EEPROM)

ESP8266WebServer server(80);

// --- Prototipos ---
String formatearTiempo(uint64_t milisegundos);
void setRele(bool activo);
void setLed(bool encendido);
void actualizarLed();
bool botonResetPulsado();
void activarRele();
void guardarTiempo();
void guardarEstado();
bool cargarEstado();
uint32_t calcularChecksum(const DatosPersistentes &datos);
void resetearConteo();
void manejarReset();
uint64_t tiempoActualMs();
void handleRaiz();
void handleEstado();
void handleResetWeb();
void handleReleWeb();
void handleObjetivoWeb();
void agregarInfoPlaca(String &json);
void construirSsid();

// --- Función Auxiliar para formatear el tiempo ---
String formatearTiempo(uint64_t milisegundos) {
  uint64_t segundos = milisegundos / 1000ULL;
  uint32_t dias = (uint32_t)(segundos / 86400ULL);
  uint32_t horas = (uint32_t)((segundos % 86400ULL) / 3600ULL);
  uint32_t minutos = (uint32_t)((segundos % 3600ULL) / 60ULL);
  uint32_t segs = (uint32_t)(segundos % 60ULL);

  char buffer[24];
  if (dias > 0) {
    snprintf(buffer, sizeof(buffer), "%ud %02u:%02u:%02u",
             dias, horas, minutos, segs);
  } else {
    snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u",
             horas, minutos, segs);
  }
  return String(buffer);
}

// --- Control del relé respetando la polaridad configurada ---
void setRele(bool activo) {
  if (RELE_ACTIVO_ALTO) {
    digitalWrite(RELE_PIN, activo ? HIGH : LOW);
  } else {
    digitalWrite(RELE_PIN, activo ? LOW : HIGH);
  }
}

// --- Control del LED interno respetando la polaridad de la placa ---
void setLed(bool encendido) {
  if (LED_ACTIVO_ALTO) {
    digitalWrite(LED_PIN, encendido ? HIGH : LOW);
  } else {
    digitalWrite(LED_PIN, encendido ? LOW : HIGH);
  }
}

// --- Parpadeo del LED interno (no bloqueante) ---
// Normal: 1 destello corto por segundo.
// Objetivo cumplido: 10 destellos rápidos, 5 s de pausa y repetir.
void actualizarLed() {
  static bool completado_anterior = false;
  static unsigned long led_inicio = 0;

  bool completado = rele_activado && (tiempo_transcurrido_total_ms >= objetivo_ms);
  unsigned long ahora = millis();

  if (completado != completado_anterior) {
    completado_anterior = completado;
    led_inicio = ahora;
  }

  unsigned long t = ahora - led_inicio;
  bool encender;

  if (!completado) {
    // Un pulso de 100 ms al inicio de cada segundo
    encender = (t % 1000UL) < 100UL;
  } else {
    // 10 parpadeos de 100 ms on / 100 ms off = 2 s, luego 5 s apagado
    unsigned long fase = t % 7000UL;
    if (fase < 2000UL) {
      encender = (fase % 200UL) < 100UL;
    } else {
      encender = false;
    }
  }

  setLed(encender);
}

// --- Lectura del botón de reset con filtro de rebotes ---
bool botonResetPulsado() {
  static unsigned long tiempo_cambio = 0;
  static bool lectura_anterior = HIGH;
  bool lectura = digitalRead(RESET_PIN);

  if (lectura != lectura_anterior) {
    lectura_anterior = lectura;
    tiempo_cambio = millis();
  }

  // Solo se considera pulsado si lleva estable en LOW el tiempo de rebote
  return (lectura == LOW && (millis() - tiempo_cambio) >= TIEMPO_REBOTE_MS);
}

// --- Página web (guardada en flash, no ocupa RAM) ---
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DaKiller</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: system-ui, sans-serif; background:#111; color:#eee;
         margin:0; display:flex; justify-content:center; padding:24px; }
  main { width:100%; max-width:420px; }
  h1 { font-size:1.4rem; margin:0 0 16px; }
  .card { background:#1c1c1e; border-radius:14px; padding:18px; margin-bottom:14px; }
  .label { color:#9a9a9e; font-size:.8rem; text-transform:uppercase; letter-spacing:.08em; }
  .big { font-size:2.4rem; font-variant-numeric:tabular-nums; margin:4px 0 14px; }
  .row { display:flex; justify-content:space-between; padding:6px 0; border-top:1px solid #2c2c2e; }
  .row:first-of-type { border-top:0; }
  .info .row { font-size:.9rem; }
  .info .row span:first-child { white-space:nowrap; }
  .info .row span:last-child { text-align:right; overflow-wrap:anywhere; margin-left:12px; }
  .activo { color:#ff453a; font-weight:600; }
  .apagado { color:#30d158; font-weight:600; }
  button { width:100%; padding:13px; margin-top:10px; border:0; border-radius:10px;
           font-size:1rem; background:#0a84ff; color:#fff; }
  button:disabled { opacity:.4; }
  button.danger { background:#ff453a; }
  button.neutral { background:#3a3a3c; }
  button.guardar { background:#30d158; color:#000; }
  .grid { display:grid; grid-template-columns:repeat(4, 1fr); gap:8px; margin-top:10px; }
  .grid label { font-size:.75rem; color:#9a9a9e; display:flex; flex-direction:column; gap:4px; }
  .grid input { width:100%; box-sizing:border-box; padding:9px; border-radius:8px;
                border:1px solid #3a3a3c; background:#2c2c2e; color:#eee; font-size:1rem; }
  #msg { min-height:1.2em; margin-top:12px; text-align:center; color:#ff9f0a; }
</style>
</head>
<body>
<main>
  <h1>DaKiller</h1>
  <div class="card">
    <div class="label">Tiempo transcurrido</div>
    <div id="tiempo" class="big">--:--:--</div>
    <div class="row"><span>Objetivo</span><span id="objetivo">--</span></div>
    <div class="row"><span>Restante</span><span id="restante">--</span></div>
    <div class="row"><span>Relé</span><span id="rele">--</span></div>
  </div>
  <div class="card info">
    <div class="label">Información de la placa</div>
    <div class="row"><span>Placa</span><span id="info_placa">--</span></div>
    <div class="row"><span>Modelo</span><span id="info_modelo">--</span></div>
    <div class="row"><span>Núcleos</span><span id="info_nucleos">--</span></div>
    <div class="row"><span>CPU</span><span id="info_cpu">--</span></div>
    <div class="row"><span>Flash</span><span id="info_flash">--</span></div>
    <div class="row"><span>MAC</span><span id="info_mac">--</span></div>
    <div class="row"><span>SDK</span><span id="info_sdk">--</span></div>
    <div class="row"><span>Core</span><span id="info_core">--</span></div>
    <div class="row"><span>Heap libre</span><span id="info_heap">--</span></div>
    <div class="row"><span>IP del AP</span><span id="info_ip">--</span></div>
    <div class="row"><span>SSID</span><span id="info_ssid">--</span></div>
    <div class="row"><span>Uptime</span><span id="info_uptime">--</span></div>
    <div class="row"><span>Firmware</span><span id="info_firmware">--</span></div>
  </div>
  <div class="card">
    <div class="label">Cambiar tiempo objetivo (máx. 99 días)</div>
    <form id="formObj" onsubmit="guardarObjetivo(event)">
      <div class="grid">
        <label>Días<input id="dias" type="number" min="0" max="99" value="0"></label>
        <label>Horas<input id="horas" type="number" min="0" max="23" value="3"></label>
        <label>Min<input id="minutos" type="number" min="0" max="59" value="0"></label>
        <label>Seg<input id="segundos" type="number" min="0" max="59" value="0"></label>
      </div>
      <button type="submit" class="guardar">Guardar objetivo</button>
    </form>
  </div>
  <button onclick="accion('/rele?estado=on')">Encender relé</button>
  <button id="btnOff" class="neutral" onclick="accion('/rele?estado=off')">Apagar relé</button>
  <button class="danger" onclick="accion('/reset')">Resetear conteo</button>
  <div id="msg"></div>
</main>
<script>
let objetivoCargado = false;
function setInfo(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = (value === undefined || value === null) ? '--' : value;
}
async function actualizar() {
  try {
    const r = await fetch('/estado');
    const d = await r.json();
    document.getElementById('tiempo').textContent = d.tiempo;
    document.getElementById('objetivo').textContent = d.objetivo;
    document.getElementById('restante').textContent = d.restante;
    const rele = document.getElementById('rele');
    rele.textContent = d.rele ? 'ACTIVADO' : 'apagado';
    rele.className = d.rele ? 'activo' : 'apagado';
    document.getElementById('btnOff').disabled = d.completado;
    setInfo('info_placa', d.placa);
    setInfo('info_modelo', d.modelo);
    setInfo('info_nucleos', d.nucleos);
    setInfo('info_cpu', d.cpu);
    setInfo('info_flash', d.flash);
    setInfo('info_mac', d.mac);
    setInfo('info_sdk', d.sdk);
    setInfo('info_core', d.core);
    setInfo('info_heap', d.heap);
    setInfo('info_ip', d.ip);
    setInfo('info_ssid', d.ssid);
    setInfo('info_uptime', d.uptime);
    setInfo('info_firmware', d.firmware);
    if (!objetivoCargado) {
      document.getElementById('dias').value = d.obj_d;
      document.getElementById('horas').value = d.obj_h;
      document.getElementById('minutos').value = d.obj_m;
      document.getElementById('segundos').value = d.obj_s;
      objetivoCargado = true;
    }
  } catch (e) {
    document.getElementById('msg').textContent = 'Sin conexión con el dispositivo';
  }
}
async function accion(url) {
  const msg = document.getElementById('msg');
  msg.textContent = '';
  try {
    const r = await fetch(url, { method: 'POST' });
    const d = await r.json();
    if (!r.ok || !d.ok) msg.textContent = d.error || 'Error';
  } catch (e) {
    msg.textContent = 'Sin conexión con el dispositivo';
  }
  actualizar();
}
async function guardarObjetivo(e) {
  e.preventDefault();
  const msg = document.getElementById('msg');
  msg.textContent = '';
  const params = new URLSearchParams({
    d: document.getElementById('dias').value,
    h: document.getElementById('horas').value,
    m: document.getElementById('minutos').value,
    s: document.getElementById('segundos').value
  });
  try {
    const r = await fetch('/objetivo', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params
    });
    const d = await r.json();
    if (!r.ok || !d.ok) {
      msg.textContent = d.error || 'Error';
    } else {
      objetivoCargado = false;
    }
  } catch (e) {
    msg.textContent = 'Sin conexión con el dispositivo';
  }
  actualizar();
}
setInterval(actualizar, 1000);
actualizar();
</script>
</body>
</html>
)HTML";

// --- Tiempo actual sin modificar el estado ---
uint64_t tiempoActualMs() {
  if (rele_activado) {
    return tiempo_transcurrido_total_ms;
  }
  return tiempo_transcurrido_total_ms + (uint32_t)(millis() - tiempo_inicio_ciclo);
}

// --- Información de la placa para la web ---
String formatearFlash(uint32_t bytes) {
  if (bytes >= 1024UL * 1024UL) {
    return String(bytes / (1024UL * 1024UL)) + " MB";
  }
  if (bytes >= 1024UL) {
    return String(bytes / 1024UL) + " KB";
  }
  return String(bytes) + " B";
}

String macToString(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

void agregarInfoPlaca(String &json) {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);

  json += ",\"placa\":\"ESP8266\"";
  json += ",\"modelo\":\"ESP8266EX\"";
  json += ",\"nucleos\":\"1\"";
  json += ",\"cpu\":\"";
  json += String((unsigned long)ESP.getCpuFreqMHz());
  json += " MHz\",\"flash\":\"";
  json += formatearFlash(ESP.getFlashChipRealSize());
  json += "\",\"mac\":\"";
  json += macToString(mac);
  json += "\",\"sdk\":\"";
  json += ESP.getSdkVersion();
  json += "\",\"core\":\"";
  json += ESP.getCoreVersion();
  json += "\",\"heap\":\"";
  json += formatearFlash(ESP.getFreeHeap());
  json += "\",\"ip\":\"";
  json += WiFi.softAPIP().toString();
  json += "\",\"ssid\":\"";
  json += AP_SSID;
  json += "\",\"uptime\":\"";
  json += formatearTiempo((uint64_t)millis());
  json += "\",\"firmware\":\"";
  json += __DATE__;
  json += " ";
  json += __TIME__;
  json += "\"";
}

// --- Handlers web ---
void handleRaiz() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleEstado() {
  uint64_t actual = tiempoActualMs();
  uint64_t restante = (actual >= objetivo_ms) ? 0 : (objetivo_ms - actual);

  uint32_t obj_seg = (uint32_t)(objetivo_ms / 1000ULL);
  uint32_t obj_d = obj_seg / 86400UL;
  uint32_t obj_h = (obj_seg % 86400UL) / 3600UL;
  uint32_t obj_m = (obj_seg % 3600UL) / 60UL;
  uint32_t obj_s = obj_seg % 60UL;

  String json;
  json.reserve(900);
  json += "{\"tiempo\":\"";
  json += formatearTiempo(actual);
  json += "\",\"objetivo\":\"";
  json += formatearTiempo(objetivo_ms);
  json += "\",\"restante\":\"";
  json += formatearTiempo(restante);
  json += "\",\"rele\":";
  json += rele_activado ? "true" : "false";
  json += ",\"completado\":";
  json += (actual >= objetivo_ms) ? "true" : "false";
  json += ",\"obj_d\":";
  json += String(obj_d);
  json += ",\"obj_h\":";
  json += String(obj_h);
  json += ",\"obj_m\":";
  json += String(obj_m);
  json += ",\"obj_s\":";
  json += String(obj_s);
  agregarInfoPlaca(json);
  json += "}";

  server.send(200, "application/json", json);
}

void handleResetWeb() {
  resetearConteo();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReleWeb() {
  String estado = server.arg("estado");

  if (estado == "on") {
    if (!rele_activado) {
      // Acumular el ciclo en curso antes de detener el conteo
      tiempo_transcurrido_total_ms += (uint32_t)(millis() - tiempo_inicio_ciclo);
      tiempo_inicio_ciclo = millis();
    }
    rele_activado = true;
    setRele(true);
    guardarEstado();
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (estado == "off") {
    // Si ya se cumplió el objetivo, apagar el relé lo reactivaría al instante.
    if (tiempo_transcurrido_total_ms >= objetivo_ms) {
      server.send(409, "application/json",
                  "{\"ok\":false,\"error\":\"Tiempo cumplido: usa Resetear conteo\"}");
      return;
    }
    rele_activado = false;
    setRele(false);
    tiempo_inicio_ciclo = millis();
    guardarEstado();
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  server.send(400, "application/json", "{\"ok\":false,\"error\":\"Estado no válido\"}");
}

void handleObjetivoWeb() {
  long d = server.arg("d").toInt();
  long h = server.arg("h").toInt();
  long m = server.arg("m").toInt();
  long s = server.arg("s").toInt();

  if (d < 0 || d > 99 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Valores fuera de rango\"}");
    return;
  }

  uint64_t nuevo = ((uint64_t)d * 86400ULL + (uint64_t)h * 3600ULL +
                    (uint64_t)m * 60ULL + (uint64_t)s) * 1000ULL;

  if (nuevo == 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"El objetivo no puede ser 0\"}");
    return;
  }
  if (nuevo > TIEMPO_MAX_OBJETIVO_MS) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"El máximo es 99 días\"}");
    return;
  }

  uint64_t objetivo_anterior = objetivo_ms;
  objetivo_ms = nuevo;

  // Si estaba activado por haber cumplido el objetivo anterior y el nuevo
  // objetivo aún no se ha alcanzado, apagamos y reanudamos el conteo.
  if (rele_activado &&
      tiempo_transcurrido_total_ms >= objetivo_anterior &&
      tiempo_transcurrido_total_ms < objetivo_ms) {
    rele_activado = false;
    setRele(false);
    tiempo_inicio_ciclo = millis();
  }

  guardarEstado();
  server.send(200, "application/json", "{\"ok\":true}");
}

// --- SSID derivado de la MAC (últimos 4 dígitos hexadecimales) ---
void construirSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(AP_SSID, sizeof(AP_SSID), "%s-%02X%02X",
           AP_SSID_BASE, mac[4], mac[5]);
}

// --- SETUP Y LOOP ---

void setup() {
  Serial.begin(115200);

  // 1. Configuración de pines.
  //    Ponemos el relé en estado seguro ANTES de habilitar el pin como salida,
  //    para evitar un disparo espurio durante el arranque.
  pinMode(RELE_PIN, OUTPUT);
  setRele(false);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  // 2. Inicializar EEPROM
  EEPROM.begin(sizeof(DatosPersistentes));

  // 3. Recuperar el tiempo, el objetivo y el ESTADO del relé
  if (!cargarEstado()) {
    objetivo_ms = TIEMPO_OBJETIVO_POR_DEFECTO_MS;
    tiempo_transcurrido_total_ms = 0;
    rele_activado = false;
    guardarEstado();
    Serial.println("EEPROM vacía o inválida: estado inicializado.");
  }

  tiempo_inicio_ciclo = millis();

  Serial.print("Tiempo objetivo: ");
  Serial.println(formatearTiempo(objetivo_ms));

  Serial.print("Tiempo recuperado de EEPROM: ");
  Serial.println(formatearTiempo(tiempo_transcurrido_total_ms));

  // 4. Verificar y aplicar el estado del relé
  if (rele_activado) {
    setRele(true);
    Serial.println("Rele estaba activo en el último apagado. Se mantiene ACTIVADO.");
    Serial.println("El conteo está detenido.");
  } else {
    setRele(false);
    Serial.println("Rele inactivo. El conteo está en curso.");
  }

  // 5. Levantar punto de acceso y servidor web
  WiFi.mode(WIFI_AP);
  construirSsid();
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, handleRaiz);
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/reset", HTTP_POST, handleResetWeb);
  server.on("/rele", HTTP_POST, handleReleWeb);
  server.on("/objetivo", HTTP_POST, handleObjetivoWeb);
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"No encontrado\"}");
  });
  server.begin();

  Serial.print("Red WiFi: ");
  Serial.println(AP_SSID);
  Serial.print("Abre http://");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
  actualizarLed();

  // 1. Verificar y manejar el reset (con debounce)
  if (botonResetPulsado()) {
    manejarReset();
  }

  // 2. Lógica del temporizador (solo si el relé NO se ha activado)
  if (!rele_activado) {
    uint32_t tiempo_ciclo_actual = (uint32_t)(millis() - tiempo_inicio_ciclo);
    uint64_t tiempo_actual = tiempo_transcurrido_total_ms + tiempo_ciclo_actual;

    if (tiempo_actual >= objetivo_ms) {
      activarRele();
    }

    // 3. Mostrar el tiempo transcurrido en serial cada segundo
    static unsigned long ultimo_reporte = 0;
    if (millis() - ultimo_reporte >= 1000) {
      ultimo_reporte = millis();
      Serial.print("Tiempo actual transcurrido: ");
      Serial.println(formatearTiempo(tiempo_actual));
    }

    // 4. Guardar el tiempo transcurrido periódicamente
    static unsigned long ultimo_guardado = millis();
    if (millis() - ultimo_guardado >= INTERVALO_GUARDADO_MS) {
      ultimo_guardado = millis();
      guardarTiempo();
    }
  }

  delay(10);
}

// --- Funciones Auxiliares ---

void activarRele() {
  if (!rele_activado) {
    // 1. Marcar y aplicar el estado
    rele_activado = true;
    setRele(true);

    // 2. DETENER EL CONTEO: forzamos el tiempo guardado a ser igual al objetivo.
    //    Esto asegura que, en el próximo reinicio, el conteo no se reanude.
    tiempo_transcurrido_total_ms = objetivo_ms;

    // 3. Guardar el tiempo y el estado en EEPROM
    guardarEstado();

    Serial.print("!!! TIEMPO CUMPLIDO (");
    Serial.print(formatearTiempo(tiempo_transcurrido_total_ms));
    Serial.println(") - RELE ACTIVADO Y CONTEO DETENIDO !!!");
  }
}

void guardarTiempo() {
  // Solo se llama si rele_activado es false (ver loop).
  // Acumulamos el tiempo del ciclo actual y reiniciamos la referencia,
  // para que la variable refleje siempre el valor guardado en EEPROM.
  tiempo_transcurrido_total_ms += (uint32_t)(millis() - tiempo_inicio_ciclo);
  tiempo_inicio_ciclo = millis();

  guardarEstado();
  Serial.print("Guardando tiempo: ");
  Serial.println(formatearTiempo(tiempo_transcurrido_total_ms));
}

uint32_t calcularChecksum(const DatosPersistentes &datos) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&datos);
  const size_t offset = offsetof(DatosPersistentes, checksum);
  uint32_t hash = 2166136261u;

  for (size_t i = 0; i < sizeof(DatosPersistentes); i++) {
    if (i >= offset && i < offset + sizeof(datos.checksum)) {
      continue;
    }
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

void guardarEstado() {
  DatosPersistentes datos;
  memset(&datos, 0, sizeof(datos));
  datos.magic = MAGIC;
  datos.version = VERSION;
  datos.objetivo_ms = objetivo_ms;
  datos.tiempo_ms = tiempo_transcurrido_total_ms;
  datos.rele_activado = rele_activado ? 1 : 0;
  datos.checksum = calcularChecksum(datos);

  EEPROM.put(0, datos);
  EEPROM.commit();
}

bool cargarEstado() {
  DatosPersistentes datos;
  EEPROM.get(0, datos);

  if (datos.magic != MAGIC ||
      datos.version != VERSION ||
      datos.checksum != calcularChecksum(datos)) {
    return false;
  }

  tiempo_transcurrido_total_ms = datos.tiempo_ms;
  rele_activado = datos.rele_activado != 0;
  objetivo_ms = datos.objetivo_ms;

  if (objetivo_ms == 0 || objetivo_ms > TIEMPO_MAX_OBJETIVO_MS) {
    objetivo_ms = TIEMPO_OBJETIVO_POR_DEFECTO_MS;
  }
  return true;
}

void resetearConteo() {
  tiempo_transcurrido_total_ms = 0;
  tiempo_inicio_ciclo = millis();
  rele_activado = false;
  setRele(false);
  guardarEstado();
}

void manejarReset() {
  Serial.println("!!! PIN DE RESET ACTIVADO - Reiniciando conteo !!!");

  resetearConteo();

  Serial.println("Conteo reseteado (00:00:00) y estado de relé borrado. Suelte el pin de reset para continuar.");

  // Espera a que se suelte el botón, con un límite de 30 s para no quedar bloqueado
  unsigned long inicio_espera = millis();
  while (digitalRead(RESET_PIN) == LOW && (millis() - inicio_espera) < 30000UL) {
    delay(10);
  }
  Serial.println("Continuando...");
}
