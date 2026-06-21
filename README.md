# Controle Automático de Exaustor por Umidade

Firmware para controle automatizado de exaustor baseado no microcontrolador Arduino e no sensor de umidade DHT22. O sistema utiliza arquitetura não-bloqueante (`millis()`) e opera através de uma máquina de estados controlada por flags e temporizadores globais.

---

## 1. Configuração dos Alvos (Setpoints)

O sistema lê dinamicamente a configuração física das chaves seletoras de hardware através da função `lerAlvo()`. A tabela abaixo define os níveis de acionamento e desligamento:

| Estado do Hardware | Nível Correspondente | Umidade Alvo de Acionamento | Regra de Desligamento (Histerese de 5%) |
| :--- | :--- | :--- | :--- |
| Nenhum pino aterrado | UMID_NIVEL0 (Padrão) | 70.00% | Umidade <= 65.0% |
| Pino D4 em GND | UMID_NIVEL1 | 60.00% | Umidade <= 55.0% |
| Pino D3 em GND | UMID_NIVEL2 | 50.00% | Umidade <= 45.0% |

* **Nota sobre Histerese (Anti-Chattering):** Para evitar oscilações rápidas no relé e desgaste do exaustor quando a umidade flutua em torno do valor alvo, o sistema exige que a umidade caia 5% abaixo do alvo para considerar o ambiente seco.

---

## 2. Máquina de Estados e Módulos de Controle Lógico

O sistema opera através de uma máquina de cinco estados lógicos principais:

### Estado 1: Monitoramento (Standby)
* **Comportamento:** O exaustor permanece desligado (`exaustorLigado = false`).
* **Ação:** Realiza leituras contínuas do sensor e compara os valores com o alvo configurado.
* **Transição:** Caso a umidade atinja ou ultrapasse o alvo e o sistema não esteja bloqueado, transiciona automaticamente para o estado de Exaustão Ativa.

### Estado 2: Exaustão Ativa
* **Comportamento:** O relé é acionado.
* **Ação:** Garante que o motor funcione por um tempo mínimo de 30 segundos (`T_MIN_ON`). 
* **Condições de Parada:** Após o tempo mínimo, o sistema avalia se a umidade retornou ao alvo (respeitando a histerese de 5%) ou se o tempo máximo de operação de 30 minutos (`T_MAX_ON`) foi atingido.

### Estado 3: Detecção de Piso Ambiente
* **Comportamento:** Atua em paralelo à exaustão ativa como medida preventiva para condições climáticas saturadas (ex: dias de chuva).
* **Ação:** Após 5 minutos com o exaustor ligado (`T_VERIFICA_QUEDA`), o sistema verifica se a umidade caiu pelo menos 3% (`QUEDA_MINIMA`).
* **Bloqueio:** Se esta queda não ocorrer, o sistema força o desligamento do exaustor, ativa a flag `bloqueioUmidade = true` e registra a leitura atual na variável `pisoBloqueio`.

### Estado 4: Desbloqueio e Cooldown
* **Condições de Desbloqueio:** O bloqueio por umidade ambiente é desfeito se:
  1. A umidade cair 1% naturalmente abaixo do `pisoBloqueio`.
  2. A umidade sofrer uma elevação abrupta de 3% (indicação de novo banho no local).
  3. A umidade cair abaixo do nível alvo original.
* **Cooldown:** Se o exaustor atingir o tempo máximo de operação de 30 minutos no estado de exaustão, o sistema entra em repouso forçado por 2 minutos (`T_COOLDOWN`) antes de permitir um novo acionamento do motor.

### Estado 5: Fallback de Erro (Safe Mode)
* **Comportamento:** Camada de redundância ativada quando o sensor para de responder e a variável `falhasSeguidas` atinge o limite máximo (`MAX_FALHAS = 5`), alterando a flag `sensorOk` para `false`.
* **Ação:** O sistema adota um ciclo cego de 30 minutos ligado e 30 minutos desligado (`T_ERRO_CICLO`) para garantir a ventilação contínua do ambiente (tratamento comum para falhas causadas por condensação no leitor).
* **Retorno:** Assim que o sensor volta a enviar leituras estáveis, o estado de emergência é cancelado e a operação normal é retomada instantaneamente.

---

## 3. Mapeamento de Pinos (Pinout)

* `D2`: Saída do Relé do Exaustor (Ativo em HIGH)
* `D3`: Entrada do Seletor Nível 2 (50% - Ativo em GND)
* `D4`: Entrada do Seletor Nível 1 (60% - Ativo em GND)
* `D5`: Saída do LED de Status (Estático = OK / Piscante = Falha no sensor)
* `D6`: Entrada de Dados do Sensor DHT22