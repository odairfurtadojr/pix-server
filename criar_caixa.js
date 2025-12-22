import axios from "axios";

// ===== CONFIGURAÇÕES =====
const ACCESS_TOKEN = process.env.ACCESS_TOKEN; // APP_USR-...
const STORE_ID = 1234567; // ID da store criada anteriormente

async function criarPOS() {
  try {
    const response = await axios.post(
      "https://api.mercadopago.com/pos",
      {
        name: "First POS",
        fixed_amount: true,
        store_id: STORE_ID,
        external_store_id: "LOJ001",
        external_id: "LOJ001POS001",
        category: 621102
      },
      {
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${ACCESS_TOKEN}`
        }
      }
    );

    console.log("✅ POS criado com sucesso:");
    console.log(response.data);
  } catch (error) {
    console.error(
      "❌ Erro ao criar POS:",
      error.response?.data || error.message
    );
  }
}

// Executa
criarPOS();
