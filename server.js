require("dotenv").config();

const express = require("express");
const axios = require("axios");
const mqtt = require("mqtt");
const cors = require("cors");

const app = express();

// ================= MIDDLEWARE =================
app.use(cors());
app.use(express.json());

// ================= CONFIGURAÇÕES =================
const PORT = process.env.PORT || 3000;
const ACCESS_TOKEN = process.env.ACCESS_TOKEN;

// 🔥 POS (CAIXA) FIXO JÁ CRIADO
const EXTERNAL_POS_ID = "LOJ001POS001"; // <-- ajuste para o seu

// 🔥 VALOR FIXO DO PRODUTO
const VALOR_FIXO = "30.00";

// ================= MQTT =================
const MQTT_BROKER = "mqtt://broker.hivemq.com";
const MQTT_TOPIC = "choppwesley/pix/status";

const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on("connect", () => {
  console.log("MQTT conectado");
});

mqttClient.on("error", (err) => {
  console.error("Erro MQTT:", err.message);
});

// ================= ROTA PRINCIPAL =================
// 👉 GERA ORDER PARA QR ESTÁTICO (PIX DINÂMICO REMOVIDO)
app.post("/criar-pagamento", async (req, res) => {
  try {
    const response = await axios.post(
      "https://api.mercadopago.com/v1/orders",
      {
        external_reference: `CHOPE_${Date.now()}`,
        title: "Chopp Pilsen 500ml",
        description: "Pagamento via QR Code Estático",
        total_amount: VALOR_FIXO,
        external_pos_id: EXTERNAL_POS_ID,
        items: [
          {
            title: "Chopp Pilsen 500ml",
            quantity: 1,
            unit_price: VALOR_FIXO
          }
        ]
      },
      {
        headers: {
          Authorization: `Bearer ${ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    res.json({
      success: true,
      order_id: response.data.id,
      external_reference: response.data.external_reference,
      message: "Escaneie o QR Code fixo do POS para pagar"
    });

  } catch (error) {
    console.error(
      "Erro ao criar order:",
      error.response?.data || error.message
    );

    res.status(500).json({
      success: false,
      error: "Erro ao criar pagamento via QR estático"
    });
  }
});

// ================= WEBHOOK MERCADO PAGO =================
app.post("/webhook", async (req, res) => {
  // RESPONDE IMEDIATO
  res.sendStatus(200);

  try {
    console.log("Webhook recebido:");
    console.log(JSON.stringify(req.body, null, 2));

    // Processa apenas orders
    if (req.body.action !== "order.processed") {
      console.log("Evento ignorado:", req.body.action);
      return;
    }

    const payments = req.body.data?.transactions?.payments;

    if (!payments || payments.length === 0) {
      console.log("Order sem pagamentos");
      return;
    }

    const payment = payments[0];

    if (
      payment.status === "processed" &&
      payment.status_detail === "accredited"
    ) {
      console.log("✅ PAGAMENTO CONFIRMADO");

      if (mqttClient.connected) {
        mqttClient.publish(MQTT_TOPIC, "PAGO");
        console.log("📡 MQTT publicado: PAGO");
      } else {
        console.log("⚠️ MQTT não conectado");
      }
    }

  } catch (error) {
    console.error(
      "Erro no processamento do webhook:",
      error.message
    );
  }
});

// ================= START SERVIDOR =================
app.listen(PORT, () => {
  console.log(`Servidor rodando na porta ${PORT}`);
});
