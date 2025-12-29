#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// ================= MQTT =================
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttTopic = "choppwesley/pix/status";

// ================= HARDWARE =================
#define RELE 5
#define BOTAO 15

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PROTÓTIPOS =================
void conectarMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect("ESP32-Chopp")) {
      Serial.println(" conectado!");
    } else {
      Serial.print(" falhou, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 2s");
      delay(2000);
    }
  }
}

void setup() {

  Serial.begin(115200);
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, LOW);
  Serial.println("\nIniciando WiFiManager...");

  // Cria o objeto WiFiManager
  WiFiManager wm;

  // Nome da rede AP que será criada se não conectar
  // Ex: ESP32-CONFIG
  bool res;
  res = wm.autoConnect("ESP32-CONFIG", "12345678");
  // senha do AP é opcional, pode remover se quiser

  if (!res) {
    Serial.println("Falha ao conectar");
    // Reinicia o ESP se não conectar
    ESP.restart();
  } 
  else {
    Serial.println("WiFi conectado com sucesso!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  conectarMQTT();
  client.publish("choppwesley/pix/acionamento", "acionado");//primeiro pedido gerado
  Serial.print("Pedido Gerado\n");
}


void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();
}

// ================= FUNÇÕES =================
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

    digitalWrite(RELE, HIGH); //Relé para acionamento da torneira
    delay(5000);
    digitalWrite(RELE, LOW);

    client.publish("choppwesley/pix/acionamento", "acionado");//gerar novo pedido
    Serial.print("Pedido Gerado\n");
  }
    if (mensagem == "EXPIRADO") {
    Serial.println("Pedido Expirado!");
    delay(200);
    client.publish("choppwesley/pix/acionamento", "acionado");//gerar novo pedido
    Serial.print("Pedido Gerado\n");
  }
}