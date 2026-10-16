#include <Preferences.h>

// Definiciones de pines
const int RELE_PIN = 27; // Pin de control del relé (ajusta a tu pin)
const int RESET_PIN = 14; // Pin para resetear la cuenta (con pull-up)
// 3 horas en milisegundos
const long TIEMPO_OBJETIVO_MS = 3L * 60L * 60L * 1000L; 

Preferences preferences; // Objeto para manejar el almacenamiento no volátil

// Variables de estado
long tiempo_transcurrido_total_ms = 0; // Almacena el tiempo total que ha estado encendido
unsigned long tiempo_inicio_ciclo = 0; // Almacena el valor de millis() al inicio del ciclo
bool rele_activado = false; // Estado actual del relé (se inicializará desde NVS)

// --- Función Auxiliar para formatear el tiempo ---
String formatearTiempo(long milisegundos) {
  long segundos = milisegundos / 1000;
  int h = segundos / 3600;
  int m = (segundos % 3600) / 60;
  int s = segundos % 60;
  
  // Usamos sprintf para formatear con ceros a la izquierda (ej: 03:05:10)
  char buffer[9]; // Suficiente para HH:MM:SS\0
  sprintf(buffer, "%02d:%02d:%02d", h, m, s);
  return String(buffer);
}

// --- SETUP Y LOOP ---

void setup() {
  Serial.begin(115200);

  // 1. Configuración de pines
  pinMode(RELE_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);

  // 2. Inicializar NVS
  preferences.begin("mi-app", false); 

  // 3. Recuperar el tiempo y el ESTADO del relé
  tiempo_transcurrido_total_ms = preferences.getLong("tiempo", 0L);
  // Almacenamos el estado del relé como un booleano (true=1, false=0)
  rele_activado = preferences.getBool("rele_state", false); 
  
  tiempo_inicio_ciclo = millis();

  Serial.print("Tiempo objetivo: ");
  Serial.println(formatearTiempo(TIEMPO_OBJETIVO_MS));

  Serial.print("Tiempo recuperado de NVS: ");
  Serial.println(formatearTiempo(tiempo_transcurrido_total_ms));

  // 4. Verificar y aplicar el estado del relé
  if (rele_activado) {
    // Si el estado guardado es true, activamos el relé al arrancar y detenemos el conteo
    digitalWrite(RELE_PIN, HIGH);
    Serial.println("Rele estaba activo en el último apagado. Se mantiene ACTIVADO.");
    Serial.println("El conteo está detenido.");
  } else {
    digitalWrite(RELE_PIN, LOW);
    Serial.println("Rele inactivo. El conteo está en curso.");
  }
}

void loop() {
  // 1. Verificar y manejar el reset
  if (digitalRead(RESET_PIN) == LOW) {
    manejarReset();
  }

  // 2. Lógica del temporizador (solo si el relé NO se ha activado)
  // Si 'rele_activado' es true, el loop no entra en esta sección, deteniendo el conteo.
  if (!rele_activado) {
    unsigned long tiempo_ciclo_actual = millis() - tiempo_inicio_ciclo;
    long tiempo_actual = tiempo_transcurrido_total_ms + tiempo_ciclo_actual;

    if (tiempo_actual >= TIEMPO_OBJETIVO_MS) {
      activarRele(); // Esto también guarda el estado
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
    digitalWrite(RELE_PIN, HIGH);
    
    // 2. DETENER EL CONTEO: Forzamos el tiempo guardado a ser igual al objetivo.
    // Esto asegura que, en el próximo reinicio, el conteo no se reanude.
    tiempo_transcurrido_total_ms = TIEMPO_OBJETIVO_MS;
    
    // 3. Guardar el tiempo y el estado en NVS
    preferences.putLong("tiempo", tiempo_transcurrido_total_ms);
    preferences.putBool("rele_state", true); // Guardar el estado activo
    
    Serial.print("!!! TIEMPO CUMPLIDO (");
    Serial.print(formatearTiempo(tiempo_transcurrido_total_ms));
    Serial.println(") - RELE ACTIVADO Y CONTEO DETENIDO !!!");
  }
}

void guardarTiempo() {
  // Esta función solo se llama si rele_activado es false (ver loop)
  long tiempo_a_guardar = tiempo_transcurrido_total_ms + (millis() - tiempo_inicio_ciclo);
  
  preferences.putLong("tiempo", tiempo_a_guardar);
  Serial.print("Guardando tiempo: ");
  Serial.println(formatearTiempo(tiempo_a_guardar));
}

void manejarReset() {
  Serial.println("!!! PIN DE RESET ACTIVADO - Reiniciando conteo !!!");
  
  // 1. Resetear variables de estado
  tiempo_transcurrido_total_ms = 0;
  tiempo_inicio_ciclo = millis();
  rele_activado = false;
  
  // 2. Apagar el relé y guardar valor inicial en NVS
  digitalWrite(RELE_PIN, LOW);
  preferences.putLong("tiempo", 0L);
  preferences.putBool("rele_state", false); // Guardar el estado inactivo
  
  Serial.println("Conteo reseteado (00:00:00) y estado de relé borrado. Suelte el pin de reset para continuar.");
  while (digitalRead(RESET_PIN) == LOW) {
    delay(100); 
  }
  Serial.println("Continuando...");
}