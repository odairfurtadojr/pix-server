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

//==================CRIAR LOJA======================

async function criarLoja() {
  try {
    const response = await axios.post(
      `https://api.mercadopago.com/users/3078863238/stores`,
      {
        name: "Loja Teste",
        external_id: "LOJATESTE",
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
          Authorization: `Bearer ${process.env.ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    console.log("✅ Loja criada com sucesso");

    return {
      id: response.data.id,
      name: response.data.name,
      external_id: response.data.external_id,
      location: response.data.location
    };

  } catch (error) {
    console.error(
      "❌ Erro ao criar loja:",
      error.response?.data || error.message
    );
    throw new Error("Falha ao criar loja no Mercado Pago");
  }
}

//================CRIAR PDV==========================

async function criarPDV() {
  try {
    const response = await axios.post(
      "https://api.mercadopago.com/pos",
      {
        name: "PDV Teste",
        fixed_amount: true,
        store_id: 72503661,
        external_store_id: "LOJATESTE",
        external_id: "LOJ001POS001"
      },
      {
        headers: {
          Authorization: `Bearer ${process.env.ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    console.log("✅ PDV criado com sucesso");

    return {
      id: response.data.id,
      name: response.data.name,
      status: response.data.status,
      store_id: response.data.store_id,
      external_id: response.data.external_id,
      qr: {
        image: response.data.qr?.image,
        template_image: response.data.qr?.template_image,
        template_document: response.data.qr?.template_document,
        qr_code: response.data.qr_code
      }
    };

  } catch (error) {
    console.error(
      "❌ Erro ao criar PDV:",
      error.response?.data || error.message
    );
    throw new Error("Falha ao criar PDV no Mercado Pago");
  }
}

//===============CRIAR ORDEM==========================

async function gerarOrdemPagamento(valor = "10.00") {
  try {
    const externalReference = uuidv4();

    const response = await axios.post(
      "https://api.mercadopago.com/v1/orders",
      {
        type: "qr",
        total_amount: valor,
        description: "PDV torneira chopp 1",
        external_reference: externalReference,
        config: {
          qr: {
            external_pos_id: "LOJ001POS001",
            mode: "static"
          }
        },
        transactions: {
          payments: [
            {
              amount: valor
            }
          ]
        }
      },
      {
        headers: {
          Authorization: `Bearer ${process.env.ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    console.log("✅ Ordem de pagamento criada");

    return {
      order_id: response.data.id,
      status: response.data.status,
      status_detail: response.data.status_detail,
      total_amount: response.data.total_amount,
      external_reference: response.data.external_reference,
      payment_id: response.data.transactions?.payments?.[0]?.id
    };

  } catch (error) {
    console.error(
      "❌ Erro ao criar ordem de pagamento:",
      error.response?.data || error.message
    );
    throw new Error("Falha ao criar ordem de pagamento no Mercado Pago");
  }
}

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
