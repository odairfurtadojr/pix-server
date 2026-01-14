#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// ================= MQTT =================
const char* mqttServer   = "mqtt.kwanan.com";
const int   mqttPort     = 1883;
const char* mqttUser     = "oda_payment";
const char* mqttPassword = "odapay@202";

// Client ID único (adicionar MAC depois)
String clientID = "ESP_WESLEY_001";

// Tópicos CORRETOS
const char* mqttTopicRequest  = "oda/payment/request/ESP_WESLEY_001";   // Publicar pedidos
const char* mqttTopicResponse = "oda/payment/response/ESP_WESLEY_001";  // Receber confirmação
const char* mqttTopicStatus   = "oda/payment/status/ESP_WESLEY_001";    // Receber status

// ================= HARDWARE =================
#define RELE  5
#define BOTAO 15

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= VARIÁVEIS =================
float amount = 10.0;

// ================= PROTÓTIPOS =================
void conectarMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void enviarPedido();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, LOW);

  WiFiManager wm;
  bool res = wm.autoConnect("ESP32-CONFIG", "12345678");

  if (!res) {
    Serial.println("Falha ao conectar no WiFi");
    ESP.restart();
  }

  Serial.println("WiFi conectado!");
  Serial.println(WiFi.localIP());

  // Gerar Client ID único com MAC address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[7];
  sprintf(macStr, "%02X%02X%02X", mac[3], mac[4], mac[5]);
  clientID = "ESP_WESLEY_001_" + String(macStr);
  
  Serial.print("Client ID: ");
  Serial.println(clientID);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(60);  // Keepalive de 60 segundos

  conectarMQTT();

  // Primeiro pedido
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
    
    // Conectar com clean_session = false (sessão persistente)
    if (mqttClient.connect(clientID.c_str(), mqttUser, mqttPassword, NULL, 0, false, NULL, false)) {
      Serial.println("Conectado!");
      
      // Subscrever nos DOIS tópicos
      mqttClient.subscribe(mqttTopicResponse);
      Serial.print("Subscrito em: ");
      Serial.println(mqttTopicResponse);
      
      mqttClient.subscribe(mqttTopicStatus);
      Serial.print("Subscrito em: ");
      Serial.println(mqttTopicStatus);
    } else {
      Serial.print("Erro: ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void enviarPedido() {
  String payload = "{\"amount\":" + String(amount, 2) + "}";

  Serial.print("Publicando em ");
  Serial.print(mqttTopicRequest);
  Serial.print(": ");
  Serial.println(payload);

  bool success = mqttClient.publish(mqttTopicRequest, payload.c_str());

  if (success) {
    Serial.println("Solicitacao enviada!");
  } else {
    Serial.println("Falha ao enviar solicitacao");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";

  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }

  Serial.print("Mensagem [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(mensagem);

  // Processar status do pagamento (approved, pending, rejected, etc.)
  if (String(topic) == mqttTopicStatus) {
    if (mensagem == "approved" || mensagem == "processed") {
      Serial.println("PAGAMENTO APROVADO!");
      digitalWrite(RELE, HIGH);
      delay(5000);
      digitalWrite(RELE, LOW);
      
      // Próximo pedido
      enviarPedido();
    }
    else if (mensagem == "rejected" || mensagem == "cancelled") {
      Serial.println("PAGAMENTO REJEITADO");
      delay(2000);
      enviarPedido();
    }
  }
  
  // Processar resposta de criação do pedido
  if (String(topic) == mqttTopicResponse) {
    if (mensagem == "created") {
      Serial.println("Pedido criado, aguardando pagamento...");
    }
  }
}