#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// ================= MQTT =================
const char* mqttServer   = "mqtt.kwanan.com";
const int   mqttPort     = 1883;
const char* mqttUser     = "oda_payment";
const char* mqttPassword = "odapay@202";

String clientID = "ESP_WESLEY_001";

const char* mqttTopicRequest  = "oda/payment/request/ESP_WESLEY_001";
const char* mqttTopicResponse = "oda/payment/response/ESP_WESLEY_001";
const char* mqttTopicStatus   = "oda/payment/status/ESP_WESLEY_001";

// ================= HARDWARE =================
#define PINO_VALVULA  5     // válvula solenoide
#define PINO_FLUXO    27    // sensor YF-S201

// ================= CHOPP =================
volatile unsigned long pulsos = 0;
float mlPorPulso = 2.22;      // AJUSTAR NA CALIBRAÇÃO
float volumeAlvo = 300.0;     // 300 ml
bool servindo = false;

// ================= VARIÁVEIS =================
float amount = 10.0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= PROTÓTIPOS =================
void conectarMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void enviarPedido();
void servirChopp();
void IRAM_ATTR contaPulso();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(PINO_VALVULA, OUTPUT);
  digitalWrite(PINO_VALVULA, LOW);

  pinMode(PINO_FLUXO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), contaPulso, FALLING);

  WiFiManager wm;
  bool res = wm.autoConnect("ESP32-CONFIG", "12345678");

  if (!res) {
    Serial.println("Falha ao conectar no WiFi");
    ESP.restart();
  }

  Serial.println("WiFi conectado!");
  Serial.println(WiFi.localIP());

  // Client ID único
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[7];
  sprintf(macStr, "%02X%02X%02X", mac[3], mac[4], mac[5]);
  clientID = "ESP_WESLEY_001_" + String(macStr);

  Serial.print("Client ID: ");
  Serial.println(clientID);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(60);

  conectarMQTT();

  enviarPedido();
}

// ================= LOOP =================
void loop() {
  if (!mqttClient.connected()) {
    conectarMQTT();
  }
  mqttClient.loop();
}

// ================= FUNÇÕES =================
void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando ao MQTT... ");

    if (mqttClient.connect(clientID.c_str(), mqttUser, mqttPassword)) {
      Serial.println("Conectado!");

      mqttClient.subscribe(mqttTopicResponse);
      mqttClient.subscribe(mqttTopicStatus);
    } else {
      Serial.print("Erro MQTT: ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void enviarPedido() {
  String payload = "{\"amount\":" + String(amount, 2) + "}";

  Serial.print("Publicando pedido: ");
  Serial.println(payload);

  mqttClient.publish(mqttTopicRequest, payload.c_str());
}

// ================= CALLBACK MQTT =================
void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";

  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }

  Serial.print("Mensagem [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(mensagem);

  // STATUS DO PAGAMENTO
  if (String(topic) == mqttTopicStatus) {

    if (mensagem == "approved" || mensagem == "processed") {
      Serial.println("PAGAMENTO APROVADO 🍺");
      servirChopp();
      enviarPedido();
    }

    else if (mensagem == "rejected" || mensagem == "cancelled") {
      Serial.println("PAGAMENTO REJEITADO ❌");
      delay(2000);
      enviarPedido();
    }
  }

  // RESPOSTA DO PEDIDO
  if (String(topic) == mqttTopicResponse) {
    if (mensagem == "created") {
      Serial.println("Pedido criado, aguardando pagamento...");
    }
  }
}

// ================= FUNÇÃO CHOPP =================
void servirChopp() {
  pulsos = 0;
  servindo = true;

  Serial.println("Abrindo válvula...");
  digitalWrite(PINO_VALVULA, HIGH);

  while ((pulsos * mlPorPulso) < volumeAlvo) {
    delay(1);
  }

  digitalWrite(PINO_VALVULA, LOW);
  servindo = false;

  Serial.print("Chopp servido: ");
  Serial.print(pulsos * mlPorPulso);
  Serial.println(" ml 🍻");
}

// ================= INTERRUPÇÃO =================
void IRAM_ATTR contaPulso() {
  pulsos++;
}
