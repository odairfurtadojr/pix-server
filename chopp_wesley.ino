#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>

// ================= MQTT =================
const char* mqttServer   = "mqtt.kwanan.com";
const int   mqttPort     = 1883;
const char* mqttUser     = "oda_payment";
const char* mqttPassword = "odapay@202";

// ⚠️ ALTERE AQUI para cada dispositivo: ESP_WESLEY_001, ESP_WESLEY_002, etc.
const char* clientID = "ESP_WESLEY_001";

String mqttTopicRequest;
String mqttTopicResponse;
String mqttTopicStatus;
String mqttTopicCalibracao;
String mqttTopicAmount;
String mqttTopicModo;
String mqttTopicResetWiFi;
String mqttTopicTotalML;
String mqttTopicResetBarril;
String mqttTopicPong;

// ================= HARDWARE =================
#define PINO_VALVULA        5
#define PINO_FLUXO          27
#define PINO_LED_VERDE      18
#define PINO_LED_VERMELHO   19

// ================= FLUXO =================
#define PULSOS_MINIMO_FLUXO 20

// ================= CHOPP =================
volatile unsigned long pulsos = 0;
volatile bool pulsoDetectado = false;

Preferences prefConfig;
Preferences prefChopp;
float mlPorPulso = 2.22;
float volumeAlvo = 300.0;
float total_ml = 0;
float ml_copo = 0;
bool servindo = false;
bool pedidoAtivo = false;
bool pedidoConfirmado = false; // true após receber "created"; suspende o timeout local
bool fluxoIniciado = false;
unsigned long tempoInicioServico = 0;
unsigned long ultimoPulso = 0;
unsigned long ultimoEncerramento = 0;
const unsigned long TIMEOUT_INICIO_SERVICO = 60000;
const unsigned long TIMEOUT_OCIOSIDADE     = 30000;
const unsigned long DELAY_NOVO_PEDIDO      = 2000;
bool precisaNovoPedido = false;

// FIX #3: flag para publicar total_ml fora do callback/encerrarServico
bool precisaPublicarTotalML = false;
float totalMLParaPublicar = 0;

// ================= FIX BUG #1: timeout do pedido ativo =================
unsigned long tempoPedido = 0;
const unsigned long TIMEOUT_PEDIDO = 30000; // 30s sem resposta → cancela pedido

// ================= VARIÁVEIS =================
float amount = 0.10;
bool modoTeste = false;
bool modoVenda = true;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= PROTÓTIPOS =================
void inicTopics();
void conectarMQTT();
void enviarPedido();
void iniciarServico();
void encerrarServico(const char* motivo);
void executarModoTeste();
void finalizouChopp();
void publicarTotalMLPendente();

void callback(char* topic, byte* payload, unsigned int length);
void IRAM_ATTR contaPulso();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  Serial.print("[INIT] clientID: ");
  Serial.println(clientID);

  // Inicializa tópicos após clientID estar definido
  inicTopics();

  prefConfig.begin("config", true);
  amount     = prefConfig.getFloat("amount",  0.10);
  mlPorPulso = prefConfig.getFloat("mlPulso", 2.22);
  prefConfig.end();

  prefChopp.begin("chopp", true);
  total_ml = prefChopp.getFloat("total_ml", 0);
  prefChopp.end();

  pinMode(PINO_VALVULA,       OUTPUT);
  pinMode(PINO_LED_VERDE,     OUTPUT);
  pinMode(PINO_LED_VERMELHO,  OUTPUT);

  digitalWrite(PINO_VALVULA,      LOW);
  digitalWrite(PINO_LED_VERDE,    LOW);
  digitalWrite(PINO_LED_VERMELHO, HIGH);

  pinMode(PINO_FLUXO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), contaPulso, FALLING);

  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("ESP32-CONFIG", "12345678")) {
    ESP.restart();
  }

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(60);

  conectarMQTT();

  if (mqttClient.connected()) {
    enviarPedido();
  }
}

// ================= TOPICS =================
void inicTopics() {
  mqttTopicRequest    = String("oda/payment/request/")   + clientID;
  mqttTopicResponse   = String("oda/payment/response/")  + clientID;
  mqttTopicStatus     = String("oda/payment/status/")    + clientID;
  mqttTopicCalibracao = String("oda/payment/config/")    + clientID + "/mlPorPulso";
  mqttTopicAmount     = String("oda/payment/config/")    + clientID + "/amount";
  mqttTopicModo       = String("oda/payment/config/")    + clientID + "/modo";
  mqttTopicResetWiFi  = String("oda/payment/config/")    + clientID + "/resetWiFi";
  mqttTopicTotalML    = String("oda/payment/total_ml/")  + clientID;
  mqttTopicResetBarril= String("oda/payment/config/")    + clientID + "/reset_barril";
  mqttTopicPong       = String("oda/payment/pong/")      + clientID;
}

// ================= LOOP =================
void loop() {

  // FIX #2: mqttClient.loop() sempre primeiro, antes de qualquer return
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Reconexão WiFi
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(10);
    return;
  }

  // FIX #1: reconexão MQTT com cooldown, não bloqueia o loop
  if (!mqttClient.connected()) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 2000) {
      lastReconnect = millis();
      Serial.println("[MQTT] Reconectando...");
      conectarMQTT();
    }
    return; // aguarda próximo ciclo; loop() não trava
  }

  // FIX #3: publica total_ml pendente fora de qualquer callback
  publicarTotalMLPendente();

  // ================= FIX BUG #1: timeout do pedido sem resposta =================
  // Timeout só se aplica antes de receber "created"; após isso o MP gerencia a expiração.
  if (pedidoAtivo && !servindo && !pedidoConfirmado && millis() - tempoPedido > TIMEOUT_PEDIDO) {
    Serial.println("[PEDIDO] Timeout — sem resposta do servidor. Reenviando.");
    pedidoAtivo = false;
    precisaNovoPedido = true;
  }

  // Novo pedido pendente
  if (precisaNovoPedido && millis() - ultimoEncerramento > DELAY_NOVO_PEDIDO) {
    if (!pedidoAtivo) {
      precisaNovoPedido = false;
      enviarPedido();
    }
  }

  // ================= MODO TESTE =================
  if (modoTeste) {
    executarModoTeste();
    return;
  }

  // ================= MODO VENDA =================
  if (pulsoDetectado) {
    pulsoDetectado = false;
    if (fluxoIniciado) ultimoPulso = millis();
  }

  if (servindo && !fluxoIniciado) {
    if (pulsos >= PULSOS_MINIMO_FLUXO) {
      fluxoIniciado = true;
      ultimoPulso = millis();
      Serial.println("[SERVICO] Fluxo iniciado");
    }
  }

  if (servindo) {
    unsigned long agora = millis();

    if (!fluxoIniciado) {
      if (agora - tempoInicioServico >= TIMEOUT_INICIO_SERVICO) {
        Serial.println("[SERVICO] Timeout início (sem fluxo)");
        encerrarServico("timeout_inicio");
      }
    } else {
      // ================= FIX BUG #2: else if evita dupla chamada =================
      // Anteriormente ambas as condições podiam chamar encerrarServico() no mesmo
      // ciclo quando ociosidade e volume atingido coincidiam.
      if (agora - ultimoPulso >= TIMEOUT_OCIOSIDADE) {
        Serial.println("[SERVICO] Timeout ociosidade");
        encerrarServico("timeout_ociosidade");
      } else if ((pulsos * mlPorPulso) >= volumeAlvo) {
        Serial.println("[SERVICO] Volume atingido");
        encerrarServico("volume_atingido");
      }
    }
  }
}

// ================= MQTT =================
void conectarMQTT() {
  if (mqttClient.connected()) return;

  Serial.print("[MQTT] Conectando... ");

  if (mqttClient.connect(clientID, mqttUser, mqttPassword)) {
    Serial.println("OK");

    // FIX #4: verifica retorno de cada subscribe individualmente
    bool ok = true;
    ok &= mqttClient.subscribe(mqttTopicResponse.c_str());
    ok &= mqttClient.subscribe(mqttTopicStatus.c_str());
    ok &= mqttClient.subscribe(mqttTopicCalibracao.c_str());
    ok &= mqttClient.subscribe(mqttTopicAmount.c_str());
    ok &= mqttClient.subscribe(mqttTopicModo.c_str());
    ok &= mqttClient.subscribe(mqttTopicResetWiFi.c_str());
    ok &= mqttClient.subscribe(mqttTopicResetBarril.c_str());

    if (!ok) {
      Serial.println("[MQTT] AVISO: um ou mais subscribes falharam — reconectando");
      mqttClient.disconnect();
    } else {
      Serial.println("[MQTT] Todos os subscribes OK");

      // Anuncia presença do dispositivo no tópico pong
      mqttClient.publish(mqttTopicPong.c_str(), "online", true);
      Serial.println("[MQTT] Pong enviado: online");
    }

  } else {
    Serial.print("[MQTT] Erro: ");
    Serial.println(mqttClient.state());
  }
}

void enviarPedido() {
  if (pedidoAtivo) {
    Serial.println("[MQTT] Pedido já ativo");
    return;
  }

  String payload = "{\"amount\":" + String(amount, 2) + "}";

  if (mqttClient.publish(mqttTopicRequest.c_str(), payload.c_str())) {
    pedidoAtivo = true;
    tempoPedido = millis(); // FIX BUG #1: marca o momento do envio para timeout
    Serial.println("[MQTT] Pedido enviado");
  } else {
    Serial.println("[MQTT] ERRO ao enviar pedido");
  }
}

// FIX #3: publicação de total_ml feita sempre fora de callbacks
void publicarTotalMLPendente() {
  if (!precisaPublicarTotalML) return;
  if (!mqttClient.connected()) return;

  char payload[50];
  snprintf(payload, sizeof(payload), "%.2f", totalMLParaPublicar);

  bool ok = mqttClient.publish(mqttTopicTotalML.c_str(), payload, true);

  Serial.print("[MQTT] total_ml publicado: ");
  Serial.println(ok ? "OK" : "FALHOU");

  if (ok) {
    precisaPublicarTotalML = false;
  }
  // se falhar, tenta novamente no próximo ciclo
}

// ================= CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(msg);

  // --- Response ---
  if (String(topic) == mqttTopicResponse) {
    if (msg == "created") {
      pedidoAtivo = true;
      pedidoConfirmado = true; // suspende timeout; MP notificará expiração via status
      precisaNovoPedido = false;
    } else if (msg == "error") {
      pedidoAtivo = false;
      pedidoConfirmado = false;
      precisaNovoPedido = true;
    }
  }

  // --- Amount ---
  if (String(topic) == mqttTopicAmount) {
    float novoAmount = msg.toFloat();
    if (novoAmount > 0) {
      amount = novoAmount;

      prefConfig.begin("config", false);
      prefConfig.putFloat("amount", amount);
      prefConfig.end();

      prefConfig.begin("config", true);
      float confirmado = prefConfig.getFloat("amount", -1);
      prefConfig.end();

      Serial.print("[CONFIG] amount salvo na flash: ");
      Serial.println(confirmado, 6);

      pedidoAtivo = false;
      ultimoEncerramento = millis();
      precisaNovoPedido = true;
    }
  }

  // --- Status ---
  // ================= FIX BUG #3 (original Bug #2): status desconhecidos logados =================
  // "approved" e "processed" abrem o serviço (comportamento documentado).
  // Status desconhecidos geram um aviso em serial em vez de serem silenciados.
  if (String(topic) == mqttTopicStatus) {
    if (msg == "approved" || msg == "processed") {
      pedidoAtivo = false;
      pedidoConfirmado = false;
      iniciarServico();
    } else if (msg == "cancelled" || msg == "rejected" || msg == "expired") {
      pedidoAtivo = false;
      pedidoConfirmado = false;
      ultimoEncerramento = millis();
      precisaNovoPedido = true;
    } else {
      // Status desconhecido: loga mas não altera o estado
      Serial.print("[STATUS] AVISO: status desconhecido recebido: ");
      Serial.println(msg);
    }
  }

  // --- Calibração ---
  if (String(topic) == mqttTopicCalibracao) {
    float novoValor = msg.toFloat();
    if (novoValor > 0.1 && novoValor < 20.0) {
      mlPorPulso = novoValor;
      prefConfig.begin("config", false);
      prefConfig.putFloat("mlPulso", mlPorPulso);
      prefConfig.end();

      Serial.print("[CALIBRACAO] Novo mlPorPulso: ");
      Serial.println(mlPorPulso, 4);
    }
  }

  // --- Modo ---
  if (String(topic) == mqttTopicModo) {
    Serial.print("[MODO] Recebido: ");
    Serial.println(msg);

    if (msg == "TESTE") {
      modoTeste = true;
      modoVenda = false;
      Serial.println("[MODO] TESTE ativado");
    } else if (msg == "VENDA") {
      modoTeste = false;
      modoVenda = true;
      servindo = false;

      digitalWrite(PINO_VALVULA,      LOW);
      digitalWrite(PINO_LED_VERDE,    LOW);
      digitalWrite(PINO_LED_VERMELHO, HIGH);

      Serial.println("[MODO] VENDA ativado");
    }
  }

  // --- Reset WiFi ---
  if (strcmp(topic, mqttTopicResetWiFi.c_str()) == 0) {
    if (msg == "reset") {
      Serial.println("[WIFI] Limpando credenciais e reiniciando...");
      WiFi.disconnect(true, true);
      delay(500);

      WiFiManager wm;
      wm.resetSettings();

      delay(1000);
      ESP.restart();
    }
  }

  // --- Reset Barril ---
  if (String(topic) == mqttTopicResetBarril) {
    if (msg == "reset_barril") {
      Serial.println("[BARRIL] Zerando contador...");

      total_ml = 0;

      prefChopp.begin("chopp", false);
      prefChopp.putFloat("total_ml", total_ml);
      prefChopp.end();

      // FIX #3: agenda publicação fora do callback
      totalMLParaPublicar   = total_ml;
      precisaPublicarTotalML = true;

      Serial.println("[BARRIL] Reset agendado para publicação");
    }
  }
}

// ================= SERVIÇO =================
void iniciarServico() {
  Serial.println("[SERVICO] Iniciando");

  pulsos = 0;
  servindo = true;
  fluxoIniciado = false;
  tempoInicioServico = millis();
  ultimoPulso = millis();

  digitalWrite(PINO_VALVULA,      HIGH);
  digitalWrite(PINO_LED_VERDE,    HIGH);
  digitalWrite(PINO_LED_VERMELHO, LOW);
}

// ================= FIX BUG #2: recebe motivo para log e proteção =================
// O parâmetro "motivo" permite rastrear a causa do encerramento no serial.
// A flag servindo é zerada logo no início para evitar que um segundo ciclo do
// loop() entre novamente nesta função enquanto ela ainda executa.
void encerrarServico(const char* motivo) {

  // Proteção contra chamada dupla no mesmo ciclo (o else if no loop() já previne
  // a maioria dos casos, mas esta guarda garante idempotência)
  if (!servindo && !pedidoAtivo) {
    Serial.println("[SERVICO] encerrarServico() chamado em estado inativo — ignorado");
    return;
  }

  Serial.print("[SERVICO] Encerrando. Motivo: ");
  Serial.println(motivo);

  servindo = false;    // zeramos ANTES de qualquer outra operação
  pedidoAtivo = false;
  pedidoConfirmado = false;

  digitalWrite(PINO_VALVULA,      LOW);
  digitalWrite(PINO_LED_VERDE,    LOW);
  digitalWrite(PINO_LED_VERMELHO, HIGH);

  if (pulsos < PULSOS_MINIMO_FLUXO) {
    Serial.println("[SERVICO] Pago mas não servido (pulsos insuficientes)");
    ultimoEncerramento = millis();
    precisaNovoPedido = true;
    return;
  }

  ml_copo = pulsos * mlPorPulso;

  Serial.print("[SERVICO] Total servido: ");
  Serial.print(ml_copo);
  Serial.println(" ml");

  finalizouChopp();

  ultimoEncerramento = millis();
  precisaNovoPedido = true;
}

// ================= MODO TESTE =================
void executarModoTeste() {
  digitalWrite(PINO_VALVULA,      HIGH);
  digitalWrite(PINO_LED_VERDE,    HIGH);
  digitalWrite(PINO_LED_VERMELHO, LOW);
}

// FIX #3: finalizouChopp() apenas atualiza estado e agenda publicação
// NÃO chama mqttClient.loop() nem publish() diretamente
void finalizouChopp() {
  Serial.println(">>> FINALIZOU CHOPP <<<");

  total_ml += ml_copo;

  prefChopp.begin("chopp", false);
  prefChopp.putFloat("total_ml", total_ml);
  prefChopp.end();

  // Agenda publicação para o próximo ciclo do loop()
  totalMLParaPublicar    = total_ml;
  precisaPublicarTotalML = true;

  Serial.print("[CHOPP] total_ml acumulado: ");
  Serial.println(total_ml, 2);
}

// ================= INTERRUPÇÃO =================
void IRAM_ATTR contaPulso() {
  pulsos++;
  pulsoDetectado = true;
}
