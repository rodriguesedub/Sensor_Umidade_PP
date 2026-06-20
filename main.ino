#include "DHT.h"

/* =========================================================
   CONTROLE AUTOMATICO DE EXAUSTOR POR UMIDADE
   - Le umidade (DHT22) e aciona o exaustor via rele
   - 3 niveis de sensibilidade via chave seletora
   - Histerese (anti-chattering) + tempo min/max ligado
   - Arquitetura nao-bloqueante (millis), sem delay no loop

   OBS: a chave geral liga/desliga NAO entra aqui.
        Ela corta a energia da fonte no hardware; com o
        modulo desligado, o interruptor de parede controla
        o exaustor diretamente.
   ========================================================= */

// ---- Modo de depuracao (1 = liga o Serial / 0 = producao) ----
#define DEBUG 0

// ---- Pinos ----
#define RELAY_PIN   2   // Rele do exaustor (aciona em HIGH)
#define NIVEL2_PIN  3   // Seletor nivel 2 (70%) - ativo em GND
#define NIVEL1_PIN  4   // Seletor nivel 1 (60%) - ativo em GND
#define LED_PIN     5   // LED de status
#define DHTPIN      6   // Dados do DHT22
#define DHTTYPE     DHT22

// ---- Niveis de umidade (%) ----
const float UMID_NIVEL0 = 70.0;  // nada conectado (padrao)
const float UMID_NIVEL1 = 60.0;  // GND no D4
const float UMID_NIVEL2 = 50.0;  // GND no D3

// ---- Parametros de controle ----
const float        HISTERESE          = 5.0;        // desliga 5% abaixo do alvo
const unsigned long T_MIN_ON          = 30000UL;    // 30 s  minimo ligado
const unsigned long T_MAX_ON          = 1800000UL;  // 30 min maximo ligado
const unsigned long T_COOLDOWN        = 120000UL;   // 2 min de descanso apos atingir o maximo
const unsigned long INTERVALO_LEITURA = 2500UL;     // DHT22 exige >= 2 s entre leituras
const unsigned long INTERVALO_PISCA   = 250UL;      // pisca do LED em caso de falha
const uint8_t       MAX_FALHAS        = 5;          // falhas seguidas p/ declarar sensor com defeito

DHT dht(DHTPIN, DHTTYPE);

// ---- Estado ----
bool     exaustorLigado = false;
bool     sensorOk       = true;   // otimista no boot (LED aceso no setup)
uint8_t  falhasSeguidas = 0;
float    ultimaUmidade  = 0.0;

unsigned long tUltimaLeitura = 0;
unsigned long tLigouEm       = 0;
unsigned long tFimCooldown   = 0;
unsigned long tUltimoPisca   = 0;
bool          ledEstado      = true;

// ---- Helpers ----
void setRele(bool ligar) {
  digitalWrite(RELAY_PIN, ligar ? HIGH : LOW);
  exaustorLigado = ligar;
}

float lerAlvo() {
  // PULLUP -> estado ATIVO e LOW (conectado ao GND)
  if (digitalRead(NIVEL2_PIN) == LOW) return UMID_NIVEL2; // 70%
  if (digitalRead(NIVEL1_PIN) == LOW) return UMID_NIVEL1; // 60%
  return UMID_NIVEL0;                                     // 50%
}

void atualizaLed() {
  if (sensorOk) {
    digitalWrite(LED_PIN, HIGH);   // aceso fixo = funcionando
    ledEstado = true;
  } else {
    // piscando = falha no DHT22 (nao-bloqueante)
    if (millis() - tUltimoPisca >= INTERVALO_PISCA) {
      tUltimoPisca = millis();
      ledEstado = !ledEstado;
      digitalWrite(LED_PIN, ledEstado ? HIGH : LOW);
    }
  }
}

void setup() {
#if DEBUG
  Serial.begin(9600);
#endif
  pinMode(LED_PIN,   OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(NIVEL1_PIN, INPUT_PULLUP);
  pinMode(NIVEL2_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, HIGH); // LED aceso no setup
  setRele(false);              // exaustor comeca desligado (estado seguro)

  delay(1000);                 // espera ~1 s o sensor estabilizar
  dht.begin();
}

void loop() {
  unsigned long agora = millis();

  // ---- LED sempre atualizado ----
  atualizaLed();

  // ---- Leitura periodica do DHT22 ----
  if (agora - tUltimaLeitura >= INTERVALO_LEITURA) {
    tUltimaLeitura = agora;
    float u = dht.readHumidity();

    if (isnan(u)) {
      // NaN isolado e normal; so consideramos defeito apos varias falhas seguidas
      if (falhasSeguidas < 255) falhasSeguidas++;
      if (falhasSeguidas >= MAX_FALHAS) sensorOk = false;
    } else {
      falhasSeguidas = 0;
      sensorOk = true;
      ultimaUmidade = u;
    }

#if DEBUG
    Serial.print("Umidade: ");
    if (sensorOk) Serial.print(ultimaUmidade); else Serial.print("FALHA");
    Serial.print(" | Alvo: ");      Serial.print(lerAlvo());
    Serial.print(" | Exaustor: ");  Serial.println(exaustorLigado ? "ON" : "OFF");
#endif
  }

  // ---- Logica de controle (roda todo loop, usa a ultima umidade lida) ----

  // Fail-safe: sensor com defeito -> DESLIGA o exaustor.
  if (!sensorOk) {
    if (exaustorLigado) setRele(false);
    return;
  }

  float alvo = lerAlvo();

  // Descanso apos atingir o tempo maximo: forca desligado
  if (agora < tFimCooldown) {
    if (exaustorLigado) setRele(false);
    return;
  }

  if (exaustorLigado) {
    unsigned long ligadoHa = agora - tLigouEm;

    if (ligadoHa >= T_MAX_ON) {
      // Atingiu 30 min ligado -> desliga e entra em descanso
      setRele(false);
      tFimCooldown = agora + T_COOLDOWN;
    } else if (ligadoHa >= T_MIN_ON && ultimaUmidade <= alvo - HISTERESE) {
      // Ja rodou o minimo E a umidade caiu o suficiente -> desliga
      setRele(false);
    }
    // senao: mantem ligado
  } else {
    if (ultimaUmidade >= alvo) {
      setRele(true);
      tLigouEm = agora;
    }
  }
}
