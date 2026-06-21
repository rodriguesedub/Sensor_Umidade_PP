#include "DHT.h"

/* =========================================================
   CONTROLE AUTOMATICO DE EXAUSTOR POR UMIDADE
   - Le umidade (DHT22) e aciona o exaustor via rele
   - 3 niveis de sensibilidade via chave seletora
   - Histerese (anti-chattering) + tempo min/max ligado
   - Rotina de Clima Saturado: Ciclos progressivos de respiro
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
const unsigned long T_MAX_ON           = 1800000UL;  // 30 min maximo ligado (teste de saturação)
const unsigned long T_COOLDOWN         = 120000UL;   // 2 min de descanso (uso normal)
const unsigned long INTERVALO_LEITURA  = 2500UL;     // >= 2 s entre leituras (DHT22)
const unsigned long INTERVALO_PISCA    = 250UL;      // pisca do LED em caso de falha
const uint8_t       MAX_FALHAS         = 5;          // falhas seguidas p/ declarar defeito

// ---- Novos Parametros: Piso Ambiente e Ciclo Saturado ----
const float         QUEDA_MINIMA       = 3.0;        // Exige 3% de queda apos 30 min, senao ativa ciclo saturado
const float         QUEDA_DESBLOQUEIO  = 1.0;        // Caiu 1% abaixo do piso? Voltou a secar (sai do ciclo)
const float         PICO_DESBLOQUEIO   = 3.0;        // Subiu 3% acima do piso? Novo banho (sai do ciclo)

// Tempos da Máquina de Estados (Modo Saturado / Chuva)
const unsigned long T_SAT_PAUSA1       = 900000UL;   // 15 min parado
const unsigned long T_SAT_LIGA1        = 900000UL;   // 15 min funcionando
const unsigned long T_SAT_PAUSA2       = 1800000UL;  // 30 min parado
const unsigned long T_SAT_LIGA2        = 420000UL;   // 7 min funcionando
const unsigned long T_SAT_PAUSA3       = 3600000UL;  // 1 h parado

// ---- Tempo de Erro do Sensor ----
const unsigned long T_ERRO_CICLO       = 1800000UL;  // 30 min ON / 30 min OFF no modo de erro

DHT dht(DHTPIN, DHTTYPE);

// ---- Estado Geral do Sistema ----
bool     exaustorLigado = false;
bool     sensorOk       = true;   
uint8_t  falhasSeguidas = 0;
float    ultimaUmidade  = 0.0;

unsigned long tUltimaLeitura = 0;
unsigned long tLigouEm       = 0;
unsigned long tFimCooldown   = 0;
unsigned long tUltimoPisca   = 0;
bool          ledEstado      = true;

// ---- Estados: Ciclo de Clima Saturado ----
float         umidadeAoLigar = 0.0;
bool          modoSaturado   = false; // true = entrou na rotina de chuva
uint8_t       passoSaturado  = 0;     // rastreia qual etapa do ciclo está rodando
unsigned long tInicioPasso   = 0;     // cronometro do passo atual
float         pisoBloqueio   = 0.0;   // guarda a umidade de quando o modo saturado começou

// ---- Estados: Fallback de Erro ----
bool          erroIniciado   = false;
unsigned long tInicioErro    = 0;

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
    if (modoSaturado) {
      Serial.print(" [SATURADO: Passo ");
      Serial.print(passoSaturado);
      Serial.print("]");
    }
    Serial.println();
#endif
  }

  // ========================================================
  // 1. FALLBACK DE ERRO NO SENSOR (Liga 30 / Desliga 30)
  // ========================================================
  if (!sensorOk) {
    if (!erroIniciado) {
      erroIniciado = true;
      setRele(true); 
      tInicioErro = agora;
    }
    if (agora - tInicioErro >= T_ERRO_CICLO) {
      tInicioErro = agora;
      setRele(!exaustorLigado);
    }
    return; // Interrompe a logica normal
  } else {
    if (erroIniciado) {
      erroIniciado = false;
      setRele(false);
      tFimCooldown = 0;
      modoSaturado = false;
    }
  }

  // ========================================================
  // 2. LOGICA DE CONTROLE NORMAL E CICLO SATURADO
  // ========================================================
  float alvo = lerAlvo();

  // Tratamento de Saída do Modo Saturado:
  // Se o ar secar naturalmente (caiu do piso), bater o alvo original, ou 
  // alguem tomar um novo banho (pico de umidade), ele aborta o ciclo.
  if (modoSaturado) {
    if (ultimaUmidade <= pisoBloqueio - QUEDA_DESBLOQUEIO ||
        ultimaUmidade >= pisoBloqueio + PICO_DESBLOQUEIO  ||
        ultimaUmidade <= alvo) {
      
      modoSaturado = false;
      passoSaturado = 0;
      setRele(false); // Desliga para avaliar as condicoes normais
      
    } else {
      // Máquina de estados do ciclo progressivo
      unsigned long tempoNoPasso = agora - tInicioPasso;
      
      switch(passoSaturado) {
        case 1: // Pausa 15 min
          if (tempoNoPasso >= T_SAT_PAUSA1) { passoSaturado = 2; tInicioPasso = agora; setRele(true); }
          break;
        case 2: // Liga 15 min
          if (tempoNoPasso >= T_SAT_LIGA1)  { passoSaturado = 3; tInicioPasso = agora; setRele(false); }
          break;
        case 3: // Pausa 30 min
          if (tempoNoPasso >= T_SAT_PAUSA2) { passoSaturado = 4; tInicioPasso = agora; setRele(true); }
          break;
        case 4: // Liga 7 min
          if (tempoNoPasso >= T_SAT_LIGA2)  { passoSaturado = 5; tInicioPasso = agora; setRele(false); }
          break;
        case 5: // Pausa 1 hora (Fica repetindo entre 4 e 5)
          if (tempoNoPasso >= T_SAT_PAUSA3) { passoSaturado = 4; tInicioPasso = agora; setRele(true); }
          break;
      }
      return; // Segura o loop aqui enquanto estiver chovendo/saturado
    }
  }

  // Cooldown normal de segurança para o motor
  if (agora < tFimCooldown) {
    if (exaustorLigado) setRele(false);
    return;
  }

  // Lógica principal de exaustão de banho
  if (exaustorLigado) {
    unsigned long ligadoHa = agora - tLigouEm;

    // Chegou no teto de 30 minutos contínuos
    if (ligadoHa >= T_MAX_ON) {
      
      // Valida se a umidade caiu o mínimo esperado. Se não caiu, o ambiente está saturado.
      if ((umidadeAoLigar - ultimaUmidade) < QUEDA_MINIMA) {
        modoSaturado = true;
        passoSaturado = 1;         // Inicia no Passo 1
        tInicioPasso = agora;
        pisoBloqueio = ultimaUmidade;
        setRele(false);            // Inicia pausando 15 min
        return;
      } else {
        // A umidade está caindo bem, mas precisa descansar o motor
        setRele(false);
        tFimCooldown = agora + T_COOLDOWN;
      }
      
    } else if (ligadoHa >= T_MIN_ON && ultimaUmidade <= alvo - HISTERESE) {
      // Secou rapidamente e atingiu o alvo
      setRele(false);
    }
    
  } else {
    // Gatilho inicial
    if (ultimaUmidade >= alvo && !modoSaturado) {
      setRele(true);
      tLigouEm = agora;
      umidadeAoLigar = ultimaUmidade;
    }
  }
}