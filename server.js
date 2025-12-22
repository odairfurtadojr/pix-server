require("dotenv").config();

const express = require("express");
const axios = require("axios");
const mqtt = require("mqtt");
const cors = require("cors");

const app = express();

// ================= MIDDLEWARES =================
app.use(cors());
app.use(express.json());

// ================= ROTAS LOCAIS =================
app.post("/criar-order", require("./criar_order"));
app.post("/criar-loja", require("./criar_loja"));
app.post("/criar-caixa", require("./criar_caixa"));

// ================= SERVIDOR =================
const PORT = 3000;
app.listen(PORT, () => {
  console.log(`Servidor rodando na porta ${PORT}`);
});

// ================= MERCADO PAGO =================
const ACCESS_TOKEN = process.env.ACCESS_TOKEN;

// ================= MQTT =================
const MQTT_BROKER = "mqtt://broker.hivemq.com";
const MQTT_TOPIC = "choppwesley/pix/status";

const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on("connect", () => {
  console.log("MQTT conectado com sucesso");
});

mqttClient.on("error", (err) => {
  console.error("Erro MQTT:", err);
});

// ================= CRIAR PIX =================
app.post("/criar-pix", async (req, res) => {
  try {
    const response = await axios.post(
      "https://api.mercadopago.com/v1/payments",
      {
        transaction_amount: 10,
        payment_method_id: "pix",
        description: "Produto ESP32",
        payer: {
          email: "cliente@teste.com"
        }
      },
      {
        headers: {
          Authorization: `Bearer ${ACCESS_TOKEN}`
        }
      }
    );

    res.json({
      payment_id: response.data.id,
      qr_code:
        response.data.point_of_interaction.transaction_data.qr_code
    });
  } catch (error) {
    console.error("Erro ao criar PIX:", error.response?.data || error.message);
    res.status(500).json({ error: "Erro ao criar PIX" });
  }
});

// ================= WEBHOOK MERCADO PAGO =================
app.post("/webhook", async (req, res) => {
  // RESPONDE IMEDIATO PARA O MERCADO PAGO
  res.sendStatus(200);

  try {
    console.log("Webhook recebido:");
    console.log(JSON.stringify(req.body, null, 2));

    // Validação básica
    if (!req.body || !req.body.data || !req.body.data.id) {
      console.log("Webhook ignorado: payload inválido");
      return;
    }

    const paymentId = req.body.data.id;

    // Consulta pagamento
    const pagamento = await axios.get(
      `https://api.mercadopago.com/v1/payments/${paymentId}`,
      {
        headers: {
          Authorization: `Bearer ${ACCESS_TOKEN}`
        }
      }
    );

    const status = pagamento.data.status;
    const statusDetail = pagamento.data.status_detail;

    console.log(`Pagamento ${paymentId} → ${status} (${statusDetail})`);

    // PIX confirmado
    if (status === "approved" || statusDetail === "accredited") {
      if (mqttClient.connected) {
        mqttClient.publish(MQTT_TOPIC, "PAGO");
        console.log("Status PAGO enviado via MQTT");
      } else {
        console.log("MQTT não conectado, não foi possível publicar");
      }
    }

  } catch (error) {
    console.error("Erro no processamento do webhook:", error.response?.data || error.message);
  }
});
