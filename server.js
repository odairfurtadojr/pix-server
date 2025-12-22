const express = require("express");
const axios = require("axios");
const mqtt = require("mqtt");
const bodyParser = require("body-parser");
const cors = require("cors");

const app = express();
app.use(bodyParser.json());
app.use(cors());

app.post('/criar-order', require('./criar_order'));
app.post('/criar-loja', require('./criar_loja'));
app.post('/criar-caixa', require('./criar_caixa'));

app.listen(3000, () => {
  console.log('Servidor rodando na porta 3000');
});

// ================= MERCADO PAGO =================
const ACCESS_TOKEN = "APP_USR-5120142283772421-121914-13f737c494b79501bc39011a982d6651-3078863238";

// ================= MQTT =================
const MQTT_BROKER = "mqtt://broker.hivemq.com";
const MQTT_TOPIC = "choppwesley/pix/status";

const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on("connect", () => {
  console.log("MQTT conectado");
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
        payer: { email: "cliente@teste.com" }
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
    console.error(error.response?.data || error.message);
    res.status(500).json({ error: "Erro ao criar PIX" });
  }
});

// ================= WEBHOOK =================
app.post("/webhook", async (req, res) => {
  const paymentId = req.body.data.id;

  const pagamento = await axios.get(
    `https://api.mercadopago.com/v1/payments/${paymentId}`,
    {
      headers: {
        Authorization: `Bearer ${process.env.ACCESS_TOKEN}`
      }
    }
  );

  if (pagamento.data.status === "approved") {
    mqttClient.publish("choppwesley/pix/status", "PAGO");
  }

  res.sendStatus(200);
});