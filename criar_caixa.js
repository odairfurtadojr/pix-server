const axios = require("axios");

// ===== CONFIGURAÇÕES =====
const ACCESS_TOKEN = process.env.ACCESS_TOKEN;
const STORE_ID = 1234567; // ID da store criada anteriormente

module.exports = async (req, res) => {
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
          Authorization: `Bearer ${ACCESS_TOKEN}`,
          "Content-Type": "application/json"
        }
      }
    );

    res.json({
      success: true,
      pos_id: response.data.id,
      data: response.data
    });

  } catch (error) {
    console.error(
      "Erro ao criar POS:",
      error.response?.data || error.message
    );

    res.status(500).json({
      success: false,
      error: "Erro ao criar caixa (POS) no Mercado Pago"
    });
  }
};
