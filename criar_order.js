const axios = require("axios");

const ACCESS_TOKEN = process.env.ACCESS_TOKEN;

module.exports = async (req, res) => {
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

    res.json({
      success: true,
      order_id: response.data.id,
      data: response.data
    });

  } catch (error) {
    console.error(
      "Erro ao criar Order:",
      error.response?.data || error.message
    );

    res.status(500).json({
      success: false,
      error: "Erro ao criar order no Mercado Pago"
    });
  }
};
