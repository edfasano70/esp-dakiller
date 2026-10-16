#include <Preferences.h>

// Definiciones de pines
const int RELE_PIN = 27;  // Pin de control del relé (ajusta a tu pin)
const int RESET_PIN = 14; // Pin para resetear la cuenta (con pull-up)

// 3 horas en milisegundos
const long TIEMPO_OBJETIVO_MS = 3L * 60L * 60L * 1000L;

// Duración mínima de una pulsación para considerarla válida (filtro de rebotes)
const unsigned long TIEMPO_REBOTE_MS = 50;

// Nivel lógico con el que el relé se ACTIVA.
// - true  -> relé activo en ALTO (HIGH = activado, la mayoría de módulos)
// - false -> relé activo en BAJO  (LOW  = activado)
const bool RELE_ACTIVO_ALTO = true;

Preferences preferences; // Objeto para manejar el almacenamiento no volátil

// Variables de estado
long tiempo_transcurrido_total_ms = 0; // Tiempo total que ha estado encendido
unsigned long tiempo_inicio_ciclo = 0; // Valor de millis() al inicio del ciclo actual
bool rele_activado = false;            // Estado actual del relé (se inicializa desde NVS)

// --- Función Auxiliar para formatear el tiempo ---
String formatearTiempo(long milisegundos) {
  long segundos = milisegundos / 1000;
  int h = segundos / 3600;
  int m = (segundos % 3600) / 60;
  int s = segundos % 60;

  // snprintf evita desbordamientos del buffer (admite horas de 3 dígitos)
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", h, m, s);
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

// --- SETUP Y LOOP ---

void setup() {
  Serial.begin(115200);

  // 1. Configuración de pines.
  //    Ponemos el relé en estado seguro ANTES de habilitar el pin como salida,
  //    para evitar un disparo espurio durante el arranque.
  setRele(false);
  pinMode(RELE_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);

  // 2. Inicializar NVS
  preferences.begin("mi-app", false);

  // 3. Recuperar el tiempo y el ESTADO del relé
  tiempo_transcurrido_total_ms = preferences.getLong("tiempo", 0L);
  rele_activado = preferences.getBool("rele_state", false);

  tiempo_inicio_ciclo = millis();

  Serial.print("Tiempo objetivo: ");
  Serial.println(formatearTiempo(TIEMPO_OBJETIVO_MS));

  Serial.print("Tiempo recuperado de NVS: ");
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
}

void loop() {
  // 1. Verificar y manejar el reset (con debounce)
  if (botonResetPulsado()) {
    manejarReset();
  }

  // 2. Lógica del temporizador (solo si el relé NO se ha activado)
  if (!rele_activado) {
    unsigned long tiempo_ciclo_actual = millis() - tiempo_inicio_ciclo;
    long tiempo_actual = tiempo_transcurrido_total_ms + tiempo_ciclo_actual;

    if (tiempo_actual >= TIEMPO_OBJETIVO_MS) {
      activarRele();
    }

    // 3. Mostrar el tiempo transcurrido en serial cada segundo
    static unsigned long ultimo_reporte = 0;
    if (millis() - ultimo_reporte >= 1000) {
      ultimo_reporte = millis();
      Serial.print("Tiempo actual transcurrido: ");
      Serial.println(formatearTiempo(tiempo_actual));
    }

    // 4. Guardar el tiempo transcurrido periódicamente (cada 10 segundos)
    static unsigned long ultimo_guardado = millis();
    if (millis() - ultimo_guardado >= 10000) {
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
    tiempo_transcurrido_total_ms = TIEMPO_OBJETIVO_MS;

    // 3. Guardar el tiempo y el estado en NVS
    preferences.putLong("tiempo", tiempo_transcurrido_total_ms);
    preferences.putBool("rele_state", true);

    Serial.print("!!! TIEMPO CUMPLIDO (");
    Serial.print(formatearTiempo(tiempo_transcurrido_total_ms));
    Serial.println(") - RELE ACTIVADO Y CONTEO DETENIDO !!!");
  }
}

void guardarTiempo() {
  // Solo se llama si rele_activado es false (ver loop).
  // Acumulamos el tiempo del ciclo actual y reiniciamos la referencia,
  // para que la variable refleje siempre el valor guardado en NVS.
  tiempo_transcurrido_total_ms += millis() - tiempo_inicio_ciclo;
  tiempo_inicio_ciclo = millis();

  preferences.putLong("tiempo", tiempo_transcurrido_total_ms);
  Serial.print("Guardando tiempo: ");
  Serial.println(formatearTiempo(tiempo_transcurrido_total_ms));
}

void manejarReset() {
  Serial.println("!!! PIN DE RESET ACTIVADO - Reiniciando conteo !!!");

  // 1. Resetear variables de estado
  tiempo_transcurrido_total_ms = 0;
  tiempo_inicio_ciclo = millis();
  rele_activado = false;

  // 2. Apagar el relé y guardar valor inicial en NVS
  setRele(false);
  preferences.putLong("tiempo", 0L);
  preferences.putBool("rele_state", false);

  Serial.println("Conteo reseteado (00:00:00) y estado de relé borrado. Suelte el pin de reset para continuar.");
  while (digitalRead(RESET_PIN) == LOW) {
    delay(100);
  }
  Serial.println("Continuando...");
}
