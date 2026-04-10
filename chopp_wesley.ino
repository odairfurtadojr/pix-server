#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>

// ================= MQTT =================
const char* mqttServer   = "mqtt.kwanan.com";
const int   mqttPort     = 1883;
const char* mqttUser     = "oda_payment";
const char* mqttPassword = "odapay@202";

String clientID = "ESP_WESLEY_001";

String mqttTopicRequest    = String("oda/payment/request/") + clientID;
String mqttTopicResponse   = String("oda/payment/response/") + clientID;
String mqttTopicStatus     = String("oda/payment/status/") + clientID;
String mqttTopicCalibracao = String("oda/payment/config/") + clientID + "/mlPorPulso";

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

Preferences preferences;
float mlPorPulso = 2.22;
float volumeAlvo = 300.0;

// ================= CONTROLE =================
bool servindo = false;
bool pedidoAtivo = false;
bool fluxoIniciado = false;

unsigned long tempoInicioServico = 0;
unsigned long ultimoPulso = 0;
unsigned long ultimoMQTT = 0;

const unsigned long TIMEOUT_INICIO_SERVICO = 60000;
const unsigned long TIMEOUT_OCIOSIDADE     = 30000;
const unsigned long INTERVALO_MQTT         = 5000;

// ================= VARIÁVEIS =================
float amount = 0.10;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= PROTÓTIPOS =================
void conectarMQTT();
void enviarPedido();
void iniciarServico();
void encerrarServico();
void callback(char* topic, byte* payload, unsigned int length);
void IRAM_ATTR contaPulso();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  preferences.begin("config", false); 

  mlPorPulso = preferences.getFloat("mlPulso", 2.22);

  pinMode(PINO_VALVULA, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);

  digitalWrite(PINO_VALVULA, LOW);
  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERMELHO, HIGH);

  pinMode(PINO_FLUXO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), contaPulso, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

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
}

// ================= LOOP =================
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(2000);
    return;
  }

  mqttClient.loop();

  if (!mqttClient.connected()) {
    if (millis() - ultimoMQTT > INTERVALO_MQTT) {
      ultimoMQTT = millis();
      conectarMQTT();
    }
  }

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
        encerrarServico();
      }
      return;
    }

    if (agora - ultimoPulso >= TIMEOUT_OCIOSIDADE) {
      Serial.println("[SERVICO] Timeout ociosidade");
      encerrarServico();
    }

    if ((pulsos * mlPorPulso) >= volumeAlvo) {
      Serial.println("[SERVICO] Volume atingido");
      encerrarServico();
    }
  }
}

// ================= MQTT =================
void conectarMQTT() {

  if (mqttClient.connected()) return;

  Serial.print("[MQTT] Conectando... ");

  if (mqttClient.connect(clientID.c_str(), mqttUser, mqttPassword)) {
    Serial.println("OK");

    mqttClient.subscribe(mqttTopicResponse.c_str());
    mqttClient.subscribe(mqttTopicStatus.c_str());
    mqttClient.subscribe(mqttTopicCalibracao.c_str());

    enviarPedido();
  } else {
    Serial.print("Erro: ");
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
    Serial.println("[MQTT] Pedido enviado");
  }
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

  if (String(topic) == mqttTopicStatus) {

    // ✅ PAGOU
    if (msg == "approved" || msg == "processed") {
      pedidoAtivo = false;  // 🔥 IMPORTANTE
      iniciarServico();
    }

    // ✅ EXPIROU / CANCELADO
    else if (msg == "cancelled" || msg == "rejected" || msg == "expired") {
      Serial.println("[PAGAMENTO] Pedido expirado/cancelado, gerando novo");
      pedidoAtivo = false;
      enviarPedido();
    }
  }

  if (String(topic) == mqttTopicCalibracao) {

    float novoValor = msg.toFloat();

    if (novoValor > 0.1 && novoValor < 20.0) {
      mlPorPulso = novoValor;
      preferences.putFloat("mlPulso", mlPorPulso);

      Serial.print("[CALIBRACAO] Novo valor: ");
      Serial.println(mlPorPulso, 4);
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

  digitalWrite(PINO_VALVULA, HIGH);
  digitalWrite(PINO_LED_VERDE, HIGH);
  digitalWrite(PINO_LED_VERMELHO, LOW);
}

void encerrarServico() {
  servindo = false;
  pedidoAtivo = false;

  digitalWrite(PINO_VALVULA, LOW);
  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERMELHO, HIGH);

  Serial.print("[SERVICO] Total servido: ");
  Serial.print(pulsos * mlPorPulso);
  Serial.println(" ml");

  enviarPedido(); // 🔥 gera novo após servir
}

// ================= INTERRUPÇÃO =================
void IRAM_ATTR contaPulso() {
  pulsos++;
  pulsoDetectado = true;
}