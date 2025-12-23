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

app.get("/qr-pdv", async (req, res) => {
  try {
    const POS_ID = 1234567; // ID numérico do POS

    const response = await axios.get(
      `https://api.mercadopago.com/pos/${POS_ID}`,
      {
        headers: {
          Authorization: `Bearer ${process.env.ACCESS_TOKEN}`
        }
      }
    );

    res.json({
      success: true,
      pos_id: response.data.id,
      qr_image: response.data.qr?.image,
      qr_template: response.data.qr?.template_document
    });

  } catch (error) {
    console.error("Erro ao buscar QR do PDV:", error.response?.data || error.message);
    res.status(500).json({ error: "Erro ao buscar QR do PDV" });
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

//=============== LEITURA DO BOTÃO ===================

mqttClient.subscribe("choppwesley/pix/botao", (err) => {
  if (err) {
    console.error("❌ Erro ao se inscrever no botão:", err);
  } else {
    console.log("📡 Inscrito em choppwesley/pix/botao");
  }
});

mqttClient.on("message", (topic, message) => {
  const payload = message.toString();
  console.log(`📥 ${topic} → ${payload}`);

  if (topic === "choppwesley/pix/botao" && payload === "pressionado") {
    console.log("🚨 Botão do PIX pressionado!");
    // 👉 aqui você dispara sua lógica
  }
});
