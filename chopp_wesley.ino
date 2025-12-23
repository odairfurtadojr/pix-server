#include <WiFi.h>
#include <PubSubClient.h>

// ================= WIFI =================
const char* ssid = "ODAIR";
const char* password = "jeremias203";

// ================= MQTT =================
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttTopic = "choppwesley/pix/status";

// ================= HARDWARE =================
#define RELE 5

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PROTÓTIPOS =================
void conectarWiFi();
void conectarMQTT();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);

  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, LOW);

  conectarWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();
}

// ================= FUNÇÕES =================

void conectarWiFi() {
  Serial.print("Conectando ao WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado com sucesso");
}

void conectarMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");

    if (client.connect("ESP32_PIX_01")) {
      Serial.println(" conectado!");
      client.subscribe(mqttTopic);
    } else {
      Serial.print(" falhou, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";

  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }

  Serial.print("Mensagem recebida: ");
  Serial.println(mensagem);

  if (mensagem == "PAGO") {
    Serial.println("Pagamento confirmado!");

    digitalWrite(RELE, HIGH);
    delay(3000);
    digitalWrite(RELE, LOW);
  }
}