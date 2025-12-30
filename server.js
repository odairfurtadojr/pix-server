// ================= IMPORTS =================
import express from "express";
import cors from "cors";
import axios from "axios";
import crypto from "crypto";
import mqtt from "mqtt";

// ================= APP =================
const app = express();
app.use(cors());
app.use(express.json());

// ================= CONFIGURAÇÕES =================
const PORT = process.env.PORT || 3000;
const ACCESS_TOKEN = process.env.ACCESS_TOKEN;

// ===== Mercado Pago =====
const MP_USER_ID = "3078863238";

// Loja
const STORE_ID = 72503661;
const EXTERNAL_STORE_ID = "LOJATESTE";

// PDV
const EXTERNAL_POS_ID = "LOJ001POS001";

// Produto
const VALOR_FIXO = 10.0;

// ===== Controle de estado =====
let ordemAtiva = null;

// ================= MQTT =================
const MQTT_BROKER =
  process.env.MQTT_BROKER_URL || "mqtt://broker.hivemq.com";

const MQTT_TOPICS = {
  ACIONAMENTO: "choppwesley/pix/acionamento",
  REEMBOLSO: "choppwesley/pix/solicitar_reembolso",
  STATUS: "choppwesley/pix/status"
};

const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on("connect", () => {
  console.log("✅ MQTT conectado");

  mqttClient.subscribe(
    [MQTT_TOPICS.ACIONAMENTO, MQTT_TOPICS.REEMBOLSO],
    err => {
      if (err) {
        console.error("❌ Erro ao assinar tópicos MQTT:", err.message);
      } else {
        console.log("📡 Tópicos MQTT assinados");
      }
    }
  );
});

mqttClient.on("error", err => {
  console.error("❌ Erro MQTT:", err.message);
});

// ================= FUNÇÃO: GERAR ORDEM =================
async function gerarOrdemPagamento() {
  const idempotencyKey = crypto.randomUUID();

  const response = await axios.post(
    "https://api.mercadopago.com/v1/orders",
    {
      type: "qr",
      total_amount: VALOR_FIXO.toFixed(2),
      description: "PDV torneira chopp 1",
      external_reference: crypto.randomUUID(),
      config: {
        qr: {
          external_pos_id: EXTERNAL_POS_ID,
          mode: "static"
        }
      },
      transactions: {
        payments: [{ amount: VALOR_FIXO.toFixed(2) }]
      }
    },
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`,
        "Content-Type": "application/json",
        "X-Idempotency-Key": idempotencyKey
      }
    }
  );

  ordemAtiva = response.data.id;
  console.log("🧾 Ordem criada:", ordemAtiva);
  return response.data;
}

// ================= FUNÇÃO: REEMBOLSAR ÚLTIMO PIX =================
async function refundLastStaticPix() {
  // 1️⃣ Buscar último pagamento PIX
  const searchResponse = await axios.get(
    "https://api.mercadopago.com/v1/payments/search",
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`
      },
      params: {
        payment_method_id: "pix",
        sort: "date_created",
        criteria: "desc",
        limit: 1
      }
    }
  );

  const results = searchResponse.data.results;

  if (!results || results.length === 0) {
    throw new Error("Nenhum pagamento PIX encontrado");
  }

  const payment = results[0];

  if (payment.status !== "approved") {
    throw new Error("Último PIX não está aprovado");
  }

  if (payment.refunds && payment.refunds.length > 0) {
    throw new Error("PIX já possui reembolso");
  }

  if (
    payment.point_of_interaction?.business_info?.sub_unit !== "qr"
  ) {
    throw new Error("Último pagamento não é PIX via QR");
  }

  // 2️⃣ Solicitar reembolso
  const refundResponse = await axios.post(
    `https://api.mercadopago.com/v1/payments/${payment.id}/refunds`,
    {},
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`,
        "Content-Type": "application/json"
      }
    }
  );

  return {
    payment_id: payment.id,
    amount: payment.transaction_amount,
    refund: refundResponse.data
  };
}

// ================= MQTT: HANDLER CENTRAL =================
mqttClient.on("message", async (topic, message) => {
  const payload = message.toString();
  console.log(`📩 MQTT recebido [${topic}]: ${payload}`);

  // ===== Solicitar reembolso =====
  if (topic === MQTT_TOPICS.REEMBOLSO) {
    try {
      const result = await refundLastStaticPix();
      mqttClient.publish(
        MQTT_TOPICS.STATUS,
        JSON.stringify({ status: "REEMBOLSO_OK", ...result })
      );
    } catch (err) {
      mqttClient.publish(
        MQTT_TOPICS.STATUS,
        JSON.stringify({ status: "ERRO_REEMBOLSO", message: err.message })
      );
    }
  }

  // ===== Acionamento PDV =====
  if (topic === MQTT_TOPICS.ACIONAMENTO && payload === "acionado") {
    if (ordemAtiva) {
      console.log("⚠️ Ordem já ativa:", ordemAtiva);
      return;
    }

    try {
      await gerarOrdemPagamento();
      mqttClient.publish(MQTT_TOPICS.STATUS, "AGUARDANDO_PAGAMENTO");
    } catch (err) {
      console.error("❌ Erro ao gerar ordem:", err.message);
      ordemAtiva = null;
    }
  }
});

// ================= WEBHOOK =================
app.post("/webhook", (req, res) => {
  res.sendStatus(200);

  try {
    const action = req.body.action;
    const payments = req.body.data?.transactions?.payments;
    if (!payments?.length) return;

    const payment = payments[0];

    if (action === "order.expired" || payment.status === "cancelled") {
      ordemAtiva = null;
      mqttClient.publish(MQTT_TOPICS.STATUS, "EXPIRADO");
    }

    if (action === "order.processed" && payment.status === "processed") {
      ordemAtiva = null;
      mqttClient.publish(MQTT_TOPICS.STATUS, "PAGO");
    }
  } catch (err) {
    console.error("❌ Erro webhook:", err.message);
  }
});

// ================= ROTAS AUX =================
app.get("/health", (_, res) => {
  res.status(200).json({ status: "ok" });
});

app.get("/", (_, res) => {
  res.send("PIX Server online 🚀");
});

// ================= START =================
app.listen(PORT, () => {
  console.log(`🚀 Servidor PIX rodando na porta ${PORT}`);
});
