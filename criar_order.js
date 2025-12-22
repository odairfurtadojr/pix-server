import axios from "axios";

const ACCESS_TOKEN = process.env.ACCESS_TOKEN;

async function criarOrder() {
  try {
    const response = await axios.post(
      "https://api.mercadopago.com/merchant_orders",
      {
        external_reference: "ORDER_ESP32_001",
        items: [
          {
            id: "PROD001",
            title: "Chopp Pilsen 500ml",
            description: "Chopp liberado via ESP32",
            category_id: "food",
            quantity: 1,
            unit_price: 10.0
          }
        ],
        payer: {
          email: "cliente@teste.com"
        },
        site_id: "MLB"
      },
      {
        headers: {
          Authorization: `Bearer ${ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    console.log("✅ Order criada com sucesso:");
    console.log(response.data);
  } catch (error) {
    console.error(
      "❌ Erro ao criar Order:",
      error.response?.data || error.message
    );
  }
}

// Executa
criarOrder();
