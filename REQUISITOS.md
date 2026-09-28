# Documento de Requisitos: Framework Modular para Comboios Lego (ESP-lego-loco) - V2.0

> **Estado do Documento:** Aprovado e Consolidado  
> **Versão do Sistema:** 2.0.0  
> **Arquitetura Base:** C++ Modular Orientado a Objetos (PlatformIO)  
> **Microcontroladores Suportados:** ESP32, ESP32-S3, ESP32-C3, ESP8266  

---

## 1. Visão Geral da Arquitetura e Modos de Operação

O sistema implementa uma rede híbrida sem fios que articula simultaneamente **Wi-Fi** (para interface de utilizador e telemetria WebSocket) e **ESP-NOW** (para controlo ultra-rápido, determinístico e de baixa latência entre microcontroladores), gerida centralmente por um nó **Master**.

### 1.1. Modos de Operação
1. **Modo Manual:**
   - O utilizador controla individualmente as velocidades (-100% a +100%), paragem/travão, iluminação (3 zonas) e desvios de agulhas (track switches) através da interface web no telemóvel ou computador.
   - Resposta em tempo real via WebSockets bidirecionais.
2. **Modo Automático:**
   - O Master assume o controlo da circulação com base num **Gestor de Cenários** (State Machine) orientado a passos definidos em ficheiros CSV guardados no LittleFS.
   - Regulação automática de velocidade para prevenção de colisões através do controlo de blocos e deteção de presença.
   - Gestão temporizada de tempos de paragem (Dwell Time) em plataformas de estações.

---

## 2. Perfis de Hardware e Responsabilidades

O ecossistema é estritamente dividido em 3 perfis funcionais com firmwares dedicados:

```
                      ┌──────────────────────────────────────┐
                      │      MASTER GATEWAY & CÉREBRO        │
                      │         (ESP32 / ESP32-S3)           │
                      │  - Servidor Web Assíncrono           │
                      │  - WebSockets em Tempo Real          │
                      │  - Gestor de Cenários CSV (LittleFS) │
                      │  - Cálculo de ETA das Estações       │
                      └──────────────────┬───────────────────┘
                                         │ ESP-NOW (Canal Sincronizado)
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
   ┌───────────────────────────┐                   ┌───────────────────────────┐
   │     LOCOMOTIVA (MÓVEL)    │                   │   ESTAÇÃO / VIA (FIXO)    │
   │  (ESP32-C3/ESP8266/ESP32) │                   │      (ESP32/ESP8266)      │
   │ - Driver Motor L9110      │                   │ - Servomotor Agulha       │
   │ - 3 Zonas LED PWM         │                   │ - Emissor IR Baliza 38kHz │
   │ - Recetor IR Balizas      │                   │ - Recetor IR Corte Feixe  │
   │ - Watchdog de Segurança   │                   │ - Ecrã OLED 0.96'' I2C    │
   └───────────────────────────┘                   └───────────────────────────┘
```

### 2.1. Master (Gateway & Cérebro - ESP32 / ESP32-S3)
- **Conetividade:**
  - Ponto de Acesso Wi-Fi autónomo (SoftAP `LegoTrain_Master`) e modo Estação (STA) para ligação a redes domésticas.
  - Servidor DNS com Captive Portal para redirecionamento imediato em telemóveis.
  - mDNS responder para acesso pelo domínio amigável `http://legoloco.local`.
  - Coordenador ESP-NOW operando no mesmo canal Wi-Fi.
- **Gestor de Cenários:**
  - CRUD (Criar, Ler, Atualizar, Apagar) e exportação/importação de ficheiros `.csv` na partição LittleFS.
  - Execução transacional de passos e desvios de fluxo (`GOTO_STEP`).
- **Lógica de Tráfego e Previsão:**
  - Cálculo contínuo dos tempos estimados de chegada (ETA) dos comboios às estações com base na velocidade atual e na posição dos blocos.
  - Transmissão periódica dos ETAs via broadcast/unicast ESP-NOW para os ecrãs das estações.
- **Tabela de Frota:**
  - Registo em memória de todos os nós ativos, estado online/offline, RSSI e nível de bateria.

### 2.2. Locomotiva (Nó Móvel - ESP32-C3, ESP8266 ou ESP32)
- **Tração:**
  - Controlo bidirecional por PWM adaptado especificamente ao chip **L9110** (pinos IA e IB).
  - Rampa progressiva de aceleração e desaceleração (simulação de massa e inércia do comboio para prevenir descarrilamentos).
  - Compensação de atrito estático inicial (*deadband compensation*).
  - Travagem elétrica ativa (ambos os pinos em nível HIGH) e paragem suave por inércia (*coast*).
- **Failsafe Watchdog:**
  - Temporizador de segurança de 4 segundos: se o nó perder comunicação com o Master, o motor desacelera e trava automaticamente para evitar acidentes.
- **Iluminação (3 Zonas Independentes):**
  - **Zona 1 (Frente):** Faróis dianteiros.
  - **Zona 2 (Trás):** Faróis traseiros vermelhos.
  - **Zona 3 (Cabine / Laterais):** Luz interior e luzes de manobra.
  - Modos suportados: Manual, Auto-Direcional (faróis automáticos conforme marcha-à-frente/marcha-atrás), Manobra (*Shunting*) e Pisca de Emergência (*Hazard Flash*).
- **Localização:**
  - 1 Recetor IR (TSOP38238 ou compatível a 38kHz) apontado para a via. Descodifica o código emitido pelas balizas da pista e envia telemetria imediata com o ID do bloco ao Master.
- **Telemetria de Bateria:** Leitura analógica da tensão da bateria LiPo com reporte periódico em milivolts.

### 2.3. Infraestrutura / Estação (Nó Fixo - ESP32 ou ESP8266)
- **Balizas de Localização e Ocupação (IR):**
  - 1 Emissor IR a 38kHz transmitindo continuamente o ID codificado do bloco (protocolo NEC).
  - 1 Recetor IR instalado em frente ao emissor através dos carris: se o feixe for interrompido pelo comboio, assinala imediatamente "Via Ocupada". Quando o feixe é restabelecido, assinala "Via Livre".
- **Agulhas (Track Switches):**
  - Controlo angular de servomotores para alternância entre via direta (`STRAIGHT`) e via divergente (`TURNOUT`).
  - Movimento suave interpolado e corte de sinal (*detach*) pós-movimento para evitar zumbidos, aquecimento e consumo de energia.
- **Display de Estação:**
  - Ecrã OLED 0.96'' (128x64 pixels via I2C, SSD1306).
  - Apresenta: Nome da Estação, Comboio esperado, Contagem decrescente do ETA em formato `mm:ss`, Estado de ocupação da linha e Semáforo gráfico de 3 aspetos (Verde, Amarelo, Vermelho).
- **Lógica de Estação:**
  - Execução de tempos de paragem programados (*Dwell Time*) com contagem regressiva visual e notificação de comboio pronto a partir.

---

## 3. Protocolo de Auto-Descoberta e Identificação

1. **Geração de Identificadores:**
   - O firmware é único por perfil. O ID nasce estritamente dos últimos bytes do endereço MAC físico da placa:
     - Locomotivas: `LOCO_XXXX` (ex: `LOCO_4B5C`)
     - Infraestrutura: `TRACK_XXXX` (ex: `TRACK_1A2B`)
     - Master: `MASTER_XXXX` (ex: `MASTER_C001`)
2. **Boot e Registo:**
   - No arranque, o nó emite um pacote broadcast ESP-NOW (`FF:FF:FF:FF:FF:FF`) do tipo `MSG_DISCOVERY_ANNOUNCE`.
   - O Master regista o nó na sua tabela interna, responde com `MSG_DISCOVERY_ACK` (informando o seu próprio MAC e canal de rádio) e atualiza a Web UI via WebSocket.
3. **Mapeamento de Nomes Amigáveis:**
   - O Master armazena em `/config/nodes.json` nomes personalizados atribuídos pelo utilizador (ex: `LOCO_4B5C` -> *"Expresso do Oriente"*).

---

## 4. Estrutura Modular Requerida (PlatformIO)

```plaintext
/
├── lib/
│   ├── ConfigStore/      # JSON/CSV no LittleFS (Cenários, Mapeamento de Nomes, Definições)
│   ├── ESPNowManager/    # Broadcast, Unicast, Callbacks, Protocolo e Gestão de Peers
│   ├── MotorController/  # Lógica PWM adaptada ao L9110 com rampas e watchdog
│   ├── LightingSystem/   # Gestão de LEDs em 3 zonas (Frente, Trás, Cabine) e efeitos
│   ├── IRTelemetry/      # Emissão 38kHz, descodificação NEC e corte de feixe (ocupação)
│   ├── TrackManager/     # Controlo de servos de agulhas e temporizadores de estação
│   ├── StationDisplay/   # Desenho no OLED 0.96'' SSD1306 (horários, semáforos, agulhas)
│   └── WebServer/        # Servidor Web Assíncrono, REST API e WebSockets
├── src/
│   ├── master/main.cpp   # Entry point do Gateway & Cérebro
│   ├── master/ScenarioEngine.h # Motor de estados para execução de cenários CSV
│   ├── master/ScenarioEngine.cpp
│   ├── loco/main.cpp     # Entry point do Comboio Móvel
│   └── track/main.cpp    # Entry point da Infraestrutura/Estação
├── data/                 # Web UI com editor CSV para LittleFS
│   ├── index.html        # Dashboard responsivo com navegação móvel
│   ├── style.css         # Estilos dark-theme com glassmorphism
│   ├── app.js            # Lógica WebSocket e controlos táteis
│   └── scenarios/        # Ficheiros CSV de demonstração
├── scripts/              # Automação de compilação e fusão de binários
│   ├── merge_bin.py      # Hook pós-compilação para gerar .bin unificados
│   └── build_all_binaries.py # Compilação em lote para todos os microcontroladores
├── tools/web-flasher/    # Instalador Web Serial e ficheiros .bin compilados
│   ├── index.html        # Interface de gravação via browser
│   ├── manifest_*.json   # Manifests para esp-web-tools
│   └── binaries/         # Binários .bin prontos para gravação
└── platformio.ini        # Ambientes de compilação (master, loco, track)
```

---

## 5. Formato de Dados do Gestor de Cenários (CSV)

O formato CSV para exportação e importação de cenários obedece à seguinte estrutura de máquina de estados:

```csv
STEP_ID, TRIGGER_TYPE, TRIGGER_VALUE, TARGET_NODE, ACTION, PARAMETER
```

### 5.1. Gatilhos (TRIGGER_TYPE)
| Gatilho | Descrição | Exemplo de TRIGGER_VALUE |
| :--- | :--- | :--- |
| `START` | Executado imediatamente no arranque do cenário | `0` |
| `IR_BEACON` | Disparado quando um comboio passa numa baliza IR da via | `5` (Baliza #5) |
| `TRACK_OCCUPIED` | Disparado pelo corte de feixe ótico de presença | `TRACK_1A2B` ou `*` (Qualquer) |
| `TRACK_CLEARED` | Disparado pela desocupação da via | `TRACK_1A2B` ou `*` |
| `TIMER` | Disparado após decorrido um intervalo de tempo | `5000` (Milissegundos) |
| `DWELL_COMPLETE` | Disparado quando termina o tempo de paragem na estação | `TRACK_1A2B` |

### 5.2. Ações (ACTION)
| Ação | Exemplo de PARAMETER | Descrição |
| :--- | :--- | :--- |
| `SET_SPEED` | `50`, `-30`, `0` | Define a velocidade (-100% a +100%) do nó alvo |
| `SET_SWITCH` | `STRAIGHT`, `TURNOUT` | Comuta a agulha para via direta ou desvio |
| `SET_LIGHTS` | `AUTO`, `FRONT_ON`, `ALL_OFF` | Configura o modo das luzes |
| `DWELL_WAIT` | `10` | Inicia paragem de 10s na estação com contagem no OLED |
| `EMERGENCY_STOP` | `0` | Paragem de emergência de todos os nós |
| `GOTO_STEP` | `1` | Salto de execução para repetição em ciclo de rotas |

---

## 6. Gravação Web e Ficheiros de Firmware (.bin)

Para permitir a gravação do microcontrolador sem recurso a IDEs ou ambientes locais de compilação, o sistema disponibiliza **3 imagens de firmware** unificadas (*merged binaries*):

### 6.1. As 3 Imagens Oficiais
1. **Master Gateway:**
   - ESP32 Standard: `master_merged.bin`
   - ESP32-S3: `master_s3_merged.bin`
2. **Locomotiva (Loco):**
   - ESP32-C3: `loco_c3_merged.bin`
   - ESP8266 (D1 Mini): `loco_esp8266_merged.bin`
   - ESP32 Standard: `loco_esp32_merged.bin`
3. **Estação & Agulhas (Station / Track):**
   - ESP32 Standard: `track_merged.bin`
   - ESP8266 (D1 Mini): `track_esp8266_merged.bin`

### 6.2. Compatibilidade com Flasher Online (ex: http://esptool.spacehuhn.com/)
- Todos os ficheiros `_merged.bin` reúnem o bootloader, tabela de partições e aplicação num único ficheiro.
- **Endereço de Gravação (Offset / Flash Address):** Sempre **`0x0000`** (ou `0x0`).
- Compatível com **Google Chrome**, **Microsoft Edge** e **Opera** via Web Serial API.

---

## 7. Interface Web do Master e Controlo Mobile-Friendly

A interface servida pelo Master Gateway é concebida especificamente para operar como comando portátil em smartphones:

### 7.1. Conetividade e Acesso Móvel
1. **Rede Wi-Fi:** O Master emite o SSID **`LegoTrain_Master`** (aberto por omissão).
2. **Captive Portal:** Ao ligar o telemóvel à rede, o sistema de DNS redireciona automaticamente para o ecrã de controlo sem necessidade de digitar URLs.
3. **Acesso Direto:**
   - Via mDNS: **`http://legoloco.local`**
   - Via IP estático do Gateway: **`http://192.168.4.1`**
4. **Modo Doméstico (STA):** Se associado ao router de casa, acedível por `http://legoloco.local` em qualquer dispositivo da rede local.

### 7.2. Ergonomia Móvel
- **Throttle Tátil do Polegar:** Slider de aceleração de grandes dimensões (-100% a +100%) com envio debounced e atualização instantânea.
- **Botões de Toque Rápido:** `STOP`, `FWD 50%`, `REV 50%` com altura mínima de 44px para toque confortável.
- **Botão E-STOP Permanente:** Botão vermelho de paragem de emergência fixo no topo e na barra móvel inferior.
- **Barra Inferior Móvel (Bottom Navigation Bar):** Alternância rápida com uma mão entre 🚂 Locomotivas, 🔀 Agulhas, 📜 Cenários, 🛰️ Frota e 📋 Logs.
- **Proteção Fallback em Memória Flash (PROGMEM):** Se o utilizador gravar apenas o ficheiro `.bin` via web flasher sem carregar a partição LittleFS, o Master arranca automaticamente uma interface móvel autónoma embutida, garantindo que o comando funciona sem falhas.

---

## 8. Tabela de Pinagens por Microcontrolador

### Locomotiva (Nó Móvel)
| Função | ESP32-C3 | ESP8266 (D1 Mini) | ESP32 DevKit |
| :--- | :--- | :--- | :--- |
| Motor L9110 (IA) | GPIO 4 | D1 (GPIO 5) | GPIO 18 |
| Motor L9110 (IB) | GPIO 5 | D2 (GPIO 4) | GPIO 19 |
| Faróis Frente | GPIO 6 | D5 (GPIO 14) | GPIO 21 |
| Faróis Trás | GPIO 7 | D6 (GPIO 12) | GPIO 22 |
| Luz Cabine/Laterais | GPIO 8 | D7 (GPIO 13) | GPIO 23 |
| Recetor IR Balizas | GPIO 3 | D3 (GPIO 0) | GPIO 15 |
| Sensor Bateria (ADC)| GPIO 0 | A0 (Analógico) | GPIO 34 |

### Infraestrutura / Estação (Nó Fixo)
| Função | ESP32 DevKit | ESP8266 (D1 Mini) |
| :--- | :--- | :--- |
| Servomotor Agulha | GPIO 18 | D4 (GPIO 2) |
| Emissor IR 38kHz (TX)| GPIO 19 | D5 (GPIO 14) |
| Recetor Feixe Ocupação (RX)| GPIO 23 | D6 (GPIO 12) |
| Ecrã OLED I2C SDA | GPIO 21 | D2 (GPIO 4) |
| Ecrã OLED I2C SCL | GPIO 22 | D1 (GPIO 5) |

---

## 9. Bibliotecas Base Homologadas
- `bblanchon/ArduinoJson` (v7.x)
- `ESPAsyncWebServer` & `AsyncTCP`
- `z3t0/IRremote` (ESP32) & `crankyoldgit/IRremoteESP8266` (ESP8266)
- `adafruit/Adafruit_SSD1306` & `adafruit/Adafruit_GFX`
- `madhephaestus/ESP32Servo` (ESP32) & `Servo.h` (ESP8266)
