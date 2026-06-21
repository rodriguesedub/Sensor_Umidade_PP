#include "DHT.h"

/* =========================================================
   CONTROLE AUTOMATICO DE EXAUSTOR POR UMIDADE
   - Le umidade (DHT22) e aciona o exaustor via rele
   - 3 niveis de sensibilidade via chave seletora
   - Histerese (anti-chattering) + tempo min/max ligado
   - Deteccao de piso ambiente: desliga se a umidade nao cair
   - Fallback de Erro: Ciclo de 30 min ON / 30 min OFF
   - Arquitetura nao-bloqueante (millis), sem delay no loop
   ========================================================= */

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

// ---- Parametros de controle padrão ----
const float         HISTERESE          = 5.0;        // desliga 5% abaixo do alvo
const unsigned long T_MIN_ON           = 30000UL;    // 30 s minimo ligado
const unsigned long T_MAX_ON           = 1800000UL;  // 30 min maximo ligado
const unsigned long T_COOLDOWN         = 120000UL;   // 2 min de descanso
const unsigned long INTERVALO_LEITURA  = 2500UL;     // >= 2 s entre leituras (DHT22)
const unsigned long INTERVALO_PISCA    = 250UL;      // pisca do LED em caso de falha
const uint8_t       MAX_FALHAS         = 5;          // falhas seguidas p/ declarar defeito

// ---- Novos Parametros: Piso Ambiente e Erro ----
const unsigned long T_VERIFICA_QUEDA   = 300000UL;   // 5 min (tempo para testar se a umidade cai)
const float         QUEDA_MINIMA       = 3.0;        // Exige 3% de queda no tempo acima
const float         QUEDA_DESBLOQUEIO  = 1.0;        // Caiu 1% abaixo do piso? Voltou a secar (desbloqueia)
const float         PICO_DESBLOQUEIO   = 3.0;        // Subiu 3% acima do piso? Novo banho (desbloqueia)
const unsigned long T_ERRO_CICLO       = 1800000UL;  // 30 min ON / 30 min OFF no modo de erro

DHT dht(DHTPIN, DHTTYPE);

// ---- Estado do Sistema ----
bool     exaustorLigado = false;
bool     sensorOk       = true;   
uint8_t  falhasSeguidas = 0;
float    ultimaUmidade  = 0.0;

unsigned long tUltimaLeitura = 0;
unsigned long tLigouEm       = 0;
unsigned long tFimCooldown   = 0;
unsigned long tUltimoPisca   = 0;
bool          ledEstado      = true;

// ---- Estados: Piso de Umidade Ambiente ----
float umidadeAoLigar  = 0.0;
bool  verificouQueda  = false;
bool  bloqueioUmidade = false; // true = desistiu de secar (umidade ambiente alta)
float pisoBloqueio    = 0.0;   // guarda o valor em que travou

// ---- Estados: Fallback de Erro ----
bool  erroIniciado = false;
unsigned long tInicioErro = 0;

// ---- Helpers ----
void setRele(bool ligar) {
  digitalWrite(RELAY_PIN, ligar ? HIGH : LOW);
  exaustorLigado = ligar;
}

float lerAlvo() {
  if (digitalRead(NIVEL2_PIN) == LOW) return UMID_NIVEL2; 
  if (digitalRead(NIVEL1_PIN) == LOW) return UMID_NIVEL1; 
  return UMID_NIVEL0;                                     
}

void atualizaLed() {
  if (sensorOk) {
    digitalWrite(LED_PIN, HIGH);
    ledEstado = true;
  } else {
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
  pinMode(LED_PIN,    OUTPUT);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(NIVEL1_PIN, INPUT_PULLUP);
  pinMode(NIVEL2_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, HIGH); 
  setRele(false);              

  delay(1000);                 
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
    Serial.print(" | Exaustor: ");  Serial.print(exaustorLigado ? "ON" : "OFF");
    if (bloqueioUmidade) Serial.print(" [BLOQUEADO POR AMBIENTE]");
    Serial.println();
#endif
  }

  // ========================================================
  // 1. FALLBACK DE ERRO NO SENSOR (Liga 30 / Desliga 30)
  // ========================================================
  if (!sensorOk) {
    if (!erroIniciado) {
      erroIniciado = true;
      setRele(true); // Começa o ciclo ligado para garantir extração imediata
      tInicioErro = agora;
    }
    // A cada 30 minutos, inverte o estado
    if (agora - tInicioErro >= T_ERRO_CICLO) {
      tInicioErro = agora;
      setRele(!exaustorLigado);
    }
    return; // Interrompe a logica normal aqui
  } else {
    // Caso o sensor volte a funcionar, limpa a flag de erro e reinicia normal
    if (erroIniciado) {
      erroIniciado = false;
      setRele(false);
      tFimCooldown = 0;
      bloqueioUmidade = false;
    }
  }

  // ========================================================
  // 2. LOGICA DE CONTROLE NORMAL (COM DETECÇÃO DE PISO)
  // ========================================================
  float alvo = lerAlvo();

  // Tratamento de Desbloqueio: 
  // O exaustor parou por "piso ambiente". Ele volta se:
  // 1. O clima secou naturalmente (caiu 1% do piso)
  // 2. Alguém tomou outro banho (subiu 3% do piso)
  // 3. A umidade despencou abaixo do alvo
  if (bloqueioUmidade) {
    if (ultimaUmidade <= pisoBloqueio - QUEDA_DESBLOQUEIO ||
        ultimaUmidade >= pisoBloqueio + PICO_DESBLOQUEIO  ||
        ultimaUmidade <= alvo) {
      bloqueioUmidade = false; 
    }
  }

  // Descanso após atingir o tempo máximo (T_MAX_ON)
  if (agora < tFimCooldown) {
    if (exaustorLigado) setRele(false);
    return;
  }

  if (exaustorLigado) {
    unsigned long ligadoHa = agora - tLigouEm;

    // --- NOVIDADE: Avalia se é umidade de banho ou do clima após 5 minutos ---
    if (ligadoHa >= T_VERIFICA_QUEDA && !verificouQueda) {
      verificouQueda = true;
      if ((umidadeAoLigar - ultimaUmidade) < QUEDA_MINIMA) {
        // O ar não tem pra onde secar. Grava o piso, desliga e bloqueia.
        setRele(false);
        bloqueioUmidade = true;
        pisoBloqueio = ultimaUmidade;
        return; // Retorna para iniciar o modo bloqueado
      }
    }

    if (ligadoHa >= T_MAX_ON) {
      // Bateu os 30 min (exaustor rodou mas ainda está úmido -> desliga 2 min)
      setRele(false);
      tFimCooldown = agora + T_COOLDOWN;
    } else if (ligadoHa >= T_MIN_ON && ultimaUmidade <= alvo - HISTERESE) {
      // Atingiu o alvo com folga de histerese
      setRele(false);
    }
    
  } else {
    // Liga o exaustor apenas se não estiver bloqueado pela umidade ambiente
    if (ultimaUmidade >= alvo && !bloqueioUmidade) {
      setRele(true);
      tLigouEm = agora;
      umidadeAoLigar = ultimaUmidade;
      verificouQueda = false;
    }
  }
}