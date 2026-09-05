let ws = null;
let reconnectTimer = null;

const proto =
  window.location.protocol === "https:" ? "wss" : "ws";

function connectWebSocket() {
  ws = new WebSocket(
    `${proto}://${window.location.host}/ws`
  );

  ws.onopen = () => {
    console.log("WebSocket connected");
  };

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);

      if (msg.type === "snapshot") {
        render(msg.data);
        return;
      }

      if (msg.type === "order_sent") {
        console.log(
          "Order accepted by FastAPI:",
          msg.order_id
        );
        return;
      }

      if (msg.type === "error") {
        console.error(
          "Order error:",
          msg.message
        );
        return;
      }

    } catch (error) {
      console.error(
        "Invalid WebSocket message:",
        error
      );
    }
  };

  ws.onerror = (error) => {
    console.error(
      "WebSocket error:",
      error
    );
  };

  ws.onclose = () => {
    console.warn(
      "WebSocket disconnected"
    );

    scheduleReconnect();
  };
}

function scheduleReconnect() {
  if (reconnectTimer !== null) {
    return;
  }

  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectWebSocket();
  }, 1000);
}


function render(data) {
  const asksBody =
    document.querySelector(
      "#asksTable tbody"
    );

  const bidsBody =
    document.querySelector(
      "#bidsTable tbody"
    );

  const tradesBody =
    document.querySelector(
      "#tradesTable tbody"
    );

  if (!asksBody || !bidsBody || !tradesBody) {
    console.error(
      "Order book or trade table not found"
    );
    return;
  }


  // ==============================================================
  // ASKS
  // ==============================================================

  asksBody.innerHTML = (data.asks || [])
    .slice()
    .reverse()
    .map(
      (level) => `
        <tr>
          <td>${Number(level.price).toFixed(2)}</td>
          <td>${Number(level.quantity)}</td>
        </tr>
      `
    )
    .join("");


  // ==============================================================
  // BIDS
  // ==============================================================

  bidsBody.innerHTML = (data.bids || [])
    .map(
      (level) => `
        <tr>
          <td>${Number(level.price).toFixed(2)}</td>
          <td>${Number(level.quantity)}</td>
        </tr>
      `
    )
    .join("");


  // ==============================================================
  // RECENT TRADES
  // ==============================================================

  tradesBody.innerHTML = (data.trades || [])
    .slice()
    .reverse()
    .map(
      (trade) => `
        <tr>
          <td>${Number(trade.price).toFixed(2)}</td>
          <td>${Number(trade.quantity)}</td>
          <td>
            ${new Date(
              Number(trade.ts) * 1000
            ).toLocaleTimeString()}
          </td>
        </tr>
      `
    )
    .join("");
}


// ================================================================
// PLACE ORDER
// ================================================================

document
  .getElementById("placeOrder")
  .addEventListener("click", () => {

    if (!ws || ws.readyState !== WebSocket.OPEN) {
      console.error(
        "WebSocket is not connected"
      );
      return;
    }

    const side =
      document.getElementById("side").value;

    const price =
      parseFloat(
        document.getElementById("price").value
      );

    const quantity =
      parseInt(
        document.getElementById("quantity").value,
        10
      );


    if (!Number.isFinite(price) || price <= 0) {
      console.error(
        "Invalid price"
      );
      return;
    }

    if (!Number.isInteger(quantity) || quantity <= 0) {
      console.error(
        "Invalid quantity"
      );
      return;
    }


    ws.send(
      JSON.stringify({
        action: "place_order",
        side: side,
        price: price,
        quantity: quantity
      })
    );

    console.log(
      "Order sent:",
      side,
      price,
      quantity
    );
  });


// ================================================================
// START
// ================================================================

connectWebSocket();