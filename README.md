# Controle Automático de Exaustor por Umidade

Firmware para controle automatizado de exaustor baseado no microcontrolador Arduino nano e no sensor de umidade DHT22. O sistema utiliza arquitetura não-bloqueante (`millis()`) e opera através de uma máquina de estados controlada por flags e temporizadores globais.

---

## 1. Configuração dos Alvos (Setpoints)

O sistema lê dinamicamente a configuração física das chaves seletoras de hardware através da função `lerAlvo()`. A tabela abaixo define os níveis de acionamento e desligamento:

| Estado do Hardware | Nível Correspondente | Umidade Alvo de Acionamento | Regra de Desligamento (Histerese de 5%) |
| --- | --- | --- | --- |
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
* **Transição:** Caso a umidade atinja ou ultrapasse o alvo e o sistema não esteja no ciclo de chuva, transiciona automaticamente para o estado de Exaustão Ativa.

### Estado 2: Exaustão Ativa

* **Comportamento:** O relé é acionado.
* **Ação:** Garante que o motor funcione por um tempo mínimo de 30 segundos (`T_MIN_ON`).
* **Condições de Parada:** Após o tempo mínimo, o sistema desliga se a umidade retornar ao alvo (respeitando a histerese). Se o sistema atingir o tempo máximo de operação contínua de 30 minutos (`T_MAX_ON`), ele avalia a necessidade de entrar no Modo Clima Saturado ou apenas realizar um Cooldown.

### Estado 3: Modo Clima Saturado (Rotina de Chuva)

* **Comportamento:** Atua como medida preventiva para evitar que o exaustor fique ligado infinitamente em dias de chuva, garantindo ventilação de respiro para não abafar o ambiente.
* **Ação (Gatilho):** Ao bater 30 minutos de exaustão contínua, o sistema verifica se a umidade caiu pelo menos 3% (`QUEDA_MINIMA`). Se não caiu, ele entende que o clima externo está úmido e ativa a flag `modoSaturado = true`, gravando o nível atual em `pisoBloqueio`.
* **Ciclo Progressivo (`passoSaturado`):** Uma máquina de estados interna executa o seguinte ciclo de respiro intermitente:
* **Passo 1:** Pausa de 15 minutos.
* **Passo 2:** Liga por 15 minutos.
* **Passo 3:** Pausa de 30 minutos.
* **Passo 4:** Liga por 7 minutos.
* **Passo 5:** Pausa de 1 hora (ao fim desta pausa, o sistema retorna ao Passo 4 em loop infinito até que o clima mude).



### Estado 4: Saída do Modo Saturado e Cooldown

* **Condições de Saída (Abortar Ciclo de Chuva):** O bloqueio do ciclo saturado é imediatamente desfeito (retornando ao controle normal) se ocorrer uma das seguintes situações:
* O ar secou naturalmente 1% abaixo do piso gravado (`QUEDA_DESBLOQUEIO`).
* A umidade sofreu uma elevação abrupta de 3% acima do piso (`PICO_DESBLOQUEIO`), indicando um novo banho no local.
* A umidade despencou abaixo do alvo original.


* **Cooldown de Segurança:** Se o exaustor trabalhou os 30 minutos do Estado 2 com sucesso (a umidade caiu bem, mas não atingiu o alvo final), o motor é desligado à força por 2 minutos (`T_COOLDOWN`) para esfriar antes de retomar a exaustão.

### Estado 5: Fallback de Erro (Safe Mode)

* **Comportamento:** Camada de redundância ativada quando o sensor para de responder e a variável `falhasSeguidas` atinge o limite máximo (`MAX_FALHAS = 5`), alterando a flag `sensorOk` para `false`.
* **Ação:** O sistema adota um ciclo cego de 30 minutos ligado e 30 minutos desligado (`T_ERRO_CICLO`) para garantir a ventilação contínua do ambiente (tratamento comum para falhas causadas por condensação severa no leitor).
* **Retorno:** Assim que o sensor volta a enviar leituras estáveis, o estado de emergência é cancelado e a operação normal é retomada instantaneamente.

---

## 3. Mapeamento de Pinos (Pinout)

* `D2`: Saída do Relé do Exaustor (Ativo em HIGH)
* `D3`: Entrada do Seletor Nível 2 (50% - Ativo em GND)
* `D4`: Entrada do Seletor Nível 1 (60% - Ativo em GND)
* `D5`: Saída do LED de Status (Estático = OK / Piscante = Falha no sensor)
* `D6`: Entrada de Dados do Sensor DHT22