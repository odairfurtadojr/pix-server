import axios from "axios";

const ACCESS_TOKEN = process.env.ACCESS_TOKEN;
const USER_ID = "3078863238"; // seu user_id MP

async function criarStore() {
  const response = await axios.post(
    `https://api.mercadopago.com/users/${USER_ID}/stores`,
    {
      name: "Loja Instore",
      external_id: "LOJ001",
      business_hours: {
        monday: [{ open: "08:00", close: "18:00" }]
      },
      location: {
        street_name: "Rua Exemplo",
        street_number: "123",
        city_name: "São Paulo",
        state_name: "SP",
        latitude: -23.55052,
        longitude: -46.633308
      }
    },
    {