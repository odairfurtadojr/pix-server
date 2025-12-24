// ================= IMPORTS =================
const express = require("express");
const axios = require("axios");
const mqtt = require("mqtt");
const cors = require("cors");
const crypto = require("crypto");

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

// PDV (ID NUMÉRICO REAL)
const POS_ID = 123256613;
const EXTERNAL_POS_ID = "LOJ001POS001";

// Produto
const VALOR_FIXO = "30.00";

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
        street_name: "Nome da rua de exemplo.",
        city_name: "São José",
        state_name: "Santa Catarina",
        latitude: -27.577686,
        longitude: -48.640945,
        reference: "Perto do Mercado Pago."
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
async function gerarOrdemPagamento(valor) {
  const idempotencyKey = crypto.randomUUID();

  const response = await axios.post(
    "https://api.mercadopago.com/v1/orders",
    {
      type: "qr",
      total_amount: valor,
      description: "PDV torneira chopp 1",
      external_reference: idempotencyKey,
      expiration_time: "PT86400S",
      config: {
        qr: {
          external_pos_id: EXTERNAL_POS_ID,
          mode: "static"
        }
      },
      transactions: {
        payments: [{ amount: valor }]
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

  console.log("🧾 Ordem criada:", response.data.id);
  return response.data;
}

// ================= FUNÇÃO: BUSCAR QR DO PDV =================
async function buscarQrPDV() {
  const response = await axios.get(
    `https://api.mercadopago.com/pos/${POS_ID}`,
    {
      headers: {
        Authorization: `Bearer ${ACCESS_TOKEN}`
      }
    }
  );

  return {
    pos_id: response.data.id,
    name: response.data.name,
    status: response.data.status,
    qr: {
      image: response.data.qr?.image,
      template_image: response.data.qr?.template_image,
      template_document: response.data.qr?.template_document
    },
    external_id: response.data.external_id,
    store_id: response.data.store_id,
    external_store_id: response.data.external_store_id
  };
}

// ================= MQTT: TRIGGER =================
mqttClient.on("message", async (topic, message) => {
  const payload = message.toString();

  if (topic === MQTT_ACIONAMENTO_TOPIC && payload === "acionado") {
    console.log("🟢 Pedido Gerado");


    try {
      const ordem = await gerarOrdemPagamento(VALOR_FIXO);
      mqttClient.publish(MQTT_STATUS_TOPIC, "AGUARDANDO_PAGAMENTO");

    } catch (err) {
      console.error(
        "❌ Erro REAL ao gerar ordem:",
        err.response?.data || err.message
      );
    }
  }
});

// ================= WEBHOOK MERCADO PAGO =================
app.post("/webhook", async (req, res) => {
  res.sendStatus(200);

  try {
    const action = req.body.action;

    const payments = req.body.data?.transactions?.payments;
    if (!payments || payments.length === 0) return;

    const payment = payments[0];

    // ===============================
    // ⏰ PEDIDO EXPIRADO
    // ===============================
    if (
      action === "order.expired" ||
      (payment.status === "cancelled" &&
       payment.status_detail === "expired")
    ) {
      console.log("⏰ PEDIDO EXPIRADO");

      ordemAtiva = null;
      mqttClient.publish(MQTT_STATUS_TOPIC, "EXPIRADO");
      return;
    }

    // ===============================
    // ✅ PAGAMENTO CONFIRMADO
    // ===============================
    if (
      action === "order.processed" &&
      payment.status === "processed" &&
      payment.status_detail === "accredited"
    ) {
      console.log("✅ PAGAMENTO CONFIRMADO");

      ordemAtiva = null;
      mqttClient.publish(MQTT_STATUS_TOPIC, "PAGO");
      return;
    }

  } catch (err) {
    console.error("❌ Erro webhook:", err.message);
  }
});

// ================= ROTAS AUXILIARES =================
app.post("/criar-loja", async (req, res) => {
  try {
    const loja = await criarLoja();
    res.json(loja);
  } catch {
    res.status(500).json({ error: "Erro ao criar loja" });
  }
});

app.post("/criar-pdv", async (req, res) => {
  try {
    const pdv = await criarPDV();
    res.json(pdv);
  } catch {
    res.status(500).json({ error: "Erro ao criar PDV" });
  }
});

// ================= START =================
app.listen(PORT, () => {
  console.log(`🚀 Servidor PIX rodando na porta ${PORT}`);
});

// ================= LOG QR NO BOOT (PARA TESTE) =================
(async () => {
  try {
    const qr = await buscarQrPDV();
    console.log("📸 QR CODE DO PDV (BOOT):", qr.qr.image);
  } catch (err) {
    console.error(
      "❌ Não foi possível buscar o QR no boot:",
      err.response?.data || err.message
    );
  }
})();
