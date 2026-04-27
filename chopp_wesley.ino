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
String mqttTopicAmount = String("oda/payment/config/") + clientID + "/amount";
String mqttTopicModo = String("oda/payment/config/") + clientID + "/modo";
String mqttTopicResetWiFi = String("oda/payment/config/") + clientID + "/resetWiFi";
String mqttTopicTotalML = String("oda/payment/total_ml/") + clientID;
String mqttTopicResetBarril = String("oda/payment/config/") + clientID + "/reset_barril";

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
float total_ml = 0;        // total acumulado
float ml_copo = 0;         // ml do chopp atual
bool servindo = false;
bool pedidoAtivo = false;
bool fluxoIniciado = false;
unsigned long tempoInicioServico = 0;
unsigned long ultimoPulso = 0;
unsigned long ultimoMQTT = 0;
const unsigned long TIMEOUT_INICIO_SERVICO = 60000;
const unsigned long TIMEOUT_OCIOSIDADE     = 30000;
const unsigned long INTERVALO_MQTT         = 5000;
unsigned long ultimoEncerramento = 0;
const unsigned long DELAY_NOVO_PEDIDO = 2000;
bool precisaNovoPedido = false;

// ================= VARIÁVEIS =================
float amount = 0.10;// valor default inicial
bool modoTeste = false; // default = modo venda
bool modoVenda = true; 

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= PROTÓTIPOS =================
void conectarMQTT();
void enviarPedido();
void iniciarServico();
void encerrarServico();
void executarModoTeste();
void finalizouChopp();

void callback(char* topic, byte* payload, unsigned int length);
void IRAM_ATTR contaPulso();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  prefConfig.begin("config", true);
  amount = prefConfig.getFloat("amount", 0.10);
  mlPorPulso = prefConfig.getFloat("mlPulso", 2.22);
  prefConfig.end();

  prefChopp.begin("chopp", true);
  total_ml = prefChopp.getFloat("total_ml", 0);
  prefChopp.end();


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

  if (mqttClient.connected()) {
  enviarPedido();
  }
}

// ================= LOOP =================
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(10);
    return;
  }

  mqttClient.loop();

  if (!mqttClient.connected()) {
  static unsigned long lastReconnect = 0;

  if (millis() - lastReconnect > 2000) {
    lastReconnect = millis();

    Serial.println("[MQTT] Reconectando...");
    conectarMQTT();
  }
}

  if (precisaNovoPedido && millis() - ultimoEncerramento > DELAY_NOVO_PEDIDO) {
    if (!pedidoAtivo) {
      precisaNovoPedido = false;
      enviarPedido();
    }
  }
  // ================= MODO TESTE =================
  if (modoTeste) {
     executarModoTeste();
  }else{
 
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
    mqttClient.subscribe(mqttTopicAmount.c_str());
    mqttClient.subscribe(mqttTopicModo.c_str());
    mqttClient.subscribe(mqttTopicResetWiFi.c_str());
    mqttClient.subscribe(mqttTopicResetBarril.c_str());
    

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
  } else {
    Serial.println("[MQTT] ERRO ao enviar pedido");
  }
}

// ================= CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("CHEGOU TOPICO: ");
  Serial.println(topic);

  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(msg);

if (String(topic) == mqttTopicResponse) {
   Serial.println("[MQTT] Response recebido");

   // 🔥 NÃO mexe no pedidoAtivo aqui
   // aqui só confirma que o pagamento foi criado
}
  if (String(topic) == mqttTopicAmount) {
      
    prefConfig.begin("config", false);
    prefConfig.putFloat("amount", amount);
    prefConfig.end();

    prefConfig.begin("config", true);
    float teste = prefConfig.getFloat("amount", -1);
    prefConfig.end();

    Serial.print("[DEBUG] LIDO DA FLASH: ");
    Serial.println(teste, 6);
    Serial.print("[MQTT] Novo amount recebido: ");
    Serial.println(amount);
    pedidoAtivo = false;
    ultimoEncerramento = millis();
    precisaNovoPedido = true;
  } 

  if (String(topic) == mqttTopicStatus) {

    // ✅ PAGOU
    if ((msg == "approved" )|| (msg == "processed")) {
      pedidoAtivo = false;
      iniciarServico();
    }

    // ✅ EXPIROU / CANCELADO
    else if (msg == "cancelled" || msg == "rejected" || msg == "expired") {
      pedidoAtivo = false;
      ultimoEncerramento = millis();
      precisaNovoPedido = true;
    }
  }


  if (String(topic) == mqttTopicCalibracao) {

    float novoValor = msg.toFloat();

    if (novoValor > 0.1 && novoValor < 20.0) {
      mlPorPulso = novoValor;
      prefChopp.putFloat("mlPulso", mlPorPulso);

      Serial.print("[CALIBRACAO] Novo valor: ");
      Serial.println(mlPorPulso, 4);
    }
  }


  if (String(topic) == mqttTopicModo) {
    Serial.print("[MODO] Recebido: ");
    Serial.println(msg);

    if (msg == "TESTE") {
      modoTeste = true;
      modoVenda = false;
      Serial.println("[MODO] TESTE ativado");
    } 
    else if (msg == "VENDA") {
       modoTeste = false;
      modoVenda = true;

      // 🔥 RESET GERAL
      servindo = false;

      digitalWrite(PINO_VALVULA, LOW);
      digitalWrite(PINO_LED_VERDE, LOW);
      digitalWrite(PINO_LED_VERMELHO, HIGH);

      Serial.println("[MODO] VENDA ativado");
    }
  }

   if (strcmp(topic, mqttTopicResetWiFi.c_str()) == 0){

    Serial.println("[WIFI] Reset solicitado via MQTT!");

    if (msg == "reset") {

      Serial.println("[WIFI] Limpando credenciais...");

      WiFi.disconnect(true, true); // apaga credenciais
      delay(1000);

      // limpa também o WiFiManager
      WiFiManager wm;
      wm.resetSettings();

      Serial.println("[WIFI] Reiniciando ESP...");
      delay(2000);
      ESP.restart();
    }
  }

  if (String(topic) == mqttTopicResetBarril) {

  Serial.println("[BARRIL] Comando recebido");

  if (msg == "reset_barril") {

    Serial.println("[BARRIL] Zerando contador...");

    total_ml = 0;

    // salva na memória
    prefChopp.putFloat("total_ml", total_ml);

    // envia pro MQTT
    char payload[50];
    snprintf(payload, sizeof(payload), "%.2f", total_ml);
    mqttClient.publish(mqttTopicTotalML.c_str(), payload);

    Serial.println("[BARRIL] Barril resetado com sucesso");
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


  // 🔥 calcula o copo aqui (DO JEITO CERTO)
  if (pulsos < PULSOS_MINIMO_FLUXO) {
    Serial.println("[SERVICO] Pago mas não servido");

    ultimoEncerramento = millis();
    precisaNovoPedido = true;

    return;
}
  ml_copo = pulsos * mlPorPulso;

  Serial.print("[SERVICO] Total servido: ");
  Serial.print(ml_copo);
  Serial.println(" ml");

  // 🔥 CHAMA AQUI (isso estava faltando)
  finalizouChopp();
  ultimoEncerramento = millis();
  precisaNovoPedido = true;
}

//================== MODO TESTE ==================

void executarModoTeste() {
  digitalWrite(PINO_VALVULA, HIGH);
  digitalWrite(PINO_LED_VERDE, HIGH);
  digitalWrite(PINO_LED_VERMELHO, LOW);
}

void finalizouChopp() {

  Serial.println(">>> FINALIZOU CHOPP <<<");
  

  // Atualiza total
  total_ml += ml_copo;

  char payload[50];
  snprintf(payload, sizeof(payload), "%.2f", total_ml);

  // Garante conexão
  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Reconectando...");
    conectarMQTT();
  }

  // Processa fila MQTT
  mqttClient.loop();

  // Envia
  bool ok = mqttClient.publish(mqttTopicTotalML.c_str(), payload, true);

  Serial.print("[MQTT] total_ml: ");
  Serial.println(ok ? "ENVIADO" : "FALHOU");

  // Mantém MQTT vivo durante o delay (melhorado)
  mqttClient.loop();
}  

// ================= INTERRUPÇÃO =================
void IRAM_ATTR contaPulso() {
  pulsos++;
  pulsoDetectado = true;
}