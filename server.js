// ================= IMPORTS =================
import express from "express";
import cors from "cors";
import axios from "axios";
import crypto from "crypto";
import dotenv from "dotenv";
import mqtt from "mqtt";

dotenv.config();

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
const POS_ID = 123256613;
const EXTERNAL_POS_ID = "LOJ001POS001";

// Produto
const VALOR_FIXO = 30.0;

// ===== Controle de estado =====
let ordemAtiva = null;

// ================= MQTT =================
const MQTT_BROKER = "mqtt://broker.hivemq.com";
const MQTT_STATUS_TOPIC = "choppwesley/pix/status";
const MQTT_ACIONAMENTO_TOPIC = "choppwesley/pix/acionamento";

const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on("connect", () => {
  console.log("✅ MQTT conectado");
  mqttClient.subscribe(MQTT_ACIONAMENTO_TOPIC);
});

mqttClient.on("error", err => {
  console.error("❌ Erro MQTT:", err.message);
});

// ================= FUNÇÃO: CRIAR LOJA =================
async function criarLoja() {
  const response = await axios.post(
    `https://api.mercadopago.com/users/${MP_USER_ID}/stores`,
    {
      name: "Loja Teste",
      external_id: EXTERNAL_STORE_ID,
      location: {
        street_number: "0123",
        street_name: "Rua Exemplo",
        city_name: "São José",
        state_name: "Santa Catarina",
        latitude: -27.577686,
        longitude: -48.640945,
        reference: "Perto do Mercado Pago"
      }
    },
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`,
        "Content-Type": "application/json"
      }
    }
  );

  console.log("✅ Loja criada");
  return response.data;
}

// ================= FUNÇÃO: CRIAR PDV =================
async function criarPDV() {
  const response = await axios.post(
    "https://api.mercadopago.com/pos",
    {
      name: "PDV Teste",
      fixed_amount: true,
      store_id: STORE_ID,
      external_store_id: EXTERNAL_STORE_ID,
      external_id: EXTERNAL_POS_ID
    },
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`,
        "Content-Type": "application/json"
      }
    }
  );

  console.log("✅ PDV criado");
  return response.data;
}

// ================= FUNÇÃO: GERAR ORDEM =================
async function gerarOrdemPagamento() {
  const uuid = crypto.randomUUID();

  const payload = {
    type: "qr",
    total_amount: VALOR_FIXO,
    description: "PDV torneira chopp 1",
    external_reference: uuid,
    expiration_time: "PT324H",
    config: {
      qr: {
        external_pos_id: EXTERNAL_POS_ID,
        mode: "static"
      }
    },
    transactions: {
      payments: [
        {
          amount: VALOR_FIXO
        }
      ]
    }
  };

  const response = await axios.post(
    "https://api.mercadopago.com/v1/orders",
    payload,
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`,
        "Content-Type": "application/json",
        "X-Idempotency-Key": uuid
      }
    }
  );

  console.log("✅ Ordem criada:", response.data.id);
  ordemAtiva = response.data.id;
  return response.data;
}

// ================= FUNÇÃO: BUSCAR QR PDV =================
async function buscarQrPDV() {
  const response = await axios.get(
    `https://api.mercadopago.com/pos/${POS_ID}`,
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`
      }
    }
  );

  return response.data.qr?.image;
}

// ================= MQTT: TRIGGER =================
mqttClient.on("message", async (topic, message) => {
  const payload = message.toString();

  if (topic === MQTT_ACIONAMENTO_TOPIC && payload === "acionado") {
    console.log("🟢 Pedido acionado via MQTT");

    if (ordemAtiva) {
      console.log("⚠️ Já existe ordem ativa");
      return;
    }

    try {
      await gerarOrdemPagamento();
      mqttClient.publish(MQTT_STATUS_TOPIC, "AGUARDANDO_PAGAMENTO");
    } catch (err) {
      console.error(
        "❌ Erro ao gerar ordem:",
        err.response?.data || err.message
      );
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

    if (
      action === "order.expired" ||
      payment.status === "cancelled"
    ) {
      console.log("⏰ Pedido expirado");
      ordemAtiva = null;
      mqttClient.publish(MQTT_STATUS_TOPIC, "EXPIRADO");
    }

    if (
      action === "order.processed" &&
      payment.status === "processed"
    ) {
      console.log("✅ Pagamento confirmado");
      ordemAtiva = null;
      mqttClient.publish(MQTT_STATUS_TOPIC, "PAGO");
    }

  } catch (err) {
    console.error("❌ Erro webhook:", err.message);
  }
});

// ================= ROTAS AUX =================
app.post("/criar-loja", async (_, res) => {
  try {
    res.json(await criarLoja());
  } catch {
    res.status(500).json({ error: "Erro ao criar loja" });
  }
});

app.post("/criar-pdv", async (_, res) => {
  try {
    res.json(await criarPDV());
  } catch {
    res.status(500).json({ error: "Erro ao criar PDV" });
  }
});

app.get("/health", (_, res) => {
  res.status(200).send("OK");
});

app.get("/", (_, res) => {
  res.send("PIX Server online 🚀");
});

// ================= START =================
app.listen(PORT, async () => {
  console.log(`🚀 Servidor PIX rodando na porta ${PORT}`);

  try {
    const qr = await buscarQrPDV();
    console.log("📸 QR CODE DO PDV:", qr);
  } catch (err) {
    console.error("❌ Erro ao buscar QR:", err.message);
  }
});
