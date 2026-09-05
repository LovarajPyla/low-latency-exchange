#!/usr/bin/env python3

"""
FastAPI visualization layer for the low-latency C++ exchange simulator.

Architecture:

    Browser
       |
       | WebSocket / JSON
       v
    FastAPI
       |
       | TCP binary protocol
       v
    C++ Gateway
       |
       | SPSC queue
       v
    C++ Matching Engine
       |
       v
    OrderBook + RiskEngine

The C++ engine is authoritative.

Python does NOT perform order matching.
It only maintains a lightweight UI projection of orders/trades
received from the C++ engine.
"""

import asyncio
import json
import struct
import time
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles


app = FastAPI(
    title="Low-Latency Exchange Simulator"
)

STATIC_DIR = Path(__file__).parent

app.mount(
    "/static",
    StaticFiles(directory=STATIC_DIR),
    name="static"
)


# ================================================================
# Binary protocol
# ================================================================

NEW_ORDER = 1
CANCEL_ORDER = 2
HEARTBEAT = 3

ACK = 10
REJECT = 11
TRADE = 12


# C++ protocol structs use packed native-width fields.
#
# NewOrderMsg:
#   uint8_t
#   uint64_t
#   uint64_t
#   uint8_t
#   int64_t
#   uint32_t
#
# Little-endian matches the development machines used for this
# project.
NEW_ORDER_FORMAT = "<BQQBqI"
ACK_FORMAT = "<BQ"
REJECT_FORMAT = "<BQB"
TRADE_FORMAT = "<BQQqI"

NEW_ORDER_SIZE = struct.calcsize(
    NEW_ORDER_FORMAT
)

ACK_SIZE = struct.calcsize(
    ACK_FORMAT
)

REJECT_SIZE = struct.calcsize(
    REJECT_FORMAT
)

TRADE_SIZE = struct.calcsize(
    TRADE_FORMAT
)


# ================================================================
# Gateway connection
# ================================================================

gateway_reader = None
gateway_writer = None

gateway_lock = asyncio.Lock()


async def connect_gateway():
    """
    Connect FastAPI to the real C++ TCP gateway.
    """

    global gateway_reader
    global gateway_writer

    async with gateway_lock:

        if gateway_writer is not None:
            return True

        try:

            reader, writer = await asyncio.open_connection(
                "127.0.0.1",
                9000
            )

            gateway_reader = reader
            gateway_writer = writer

            print(
                "Connected to C++ gateway "
                "127.0.0.1:9000"
            )

            return True

        except OSError as exc:

            print(
                "C++ gateway unavailable:",
                exc
            )

            gateway_reader = None
            gateway_writer = None

            return False


async def disconnect_gateway():
    """
    Close the FastAPI -> C++ connection.
    """

    global gateway_reader
    global gateway_writer

    writer = gateway_writer

    gateway_reader = None
    gateway_writer = None

    if writer is not None:

        writer.close()

        try:
            await writer.wait_closed()
        except Exception:
            pass


# ================================================================
# UI state
# ================================================================

connected_clients: set[WebSocket] = set()

# Orders that have been sent to C++ but for which the ACK has not
# yet been processed.
pending_orders = {}

# Orders that remain on the C++ order book.
active_orders = {}

# Recent trades received from C++.
recent_trades = []

next_order_id = 1
next_sequence = 1

state_lock = asyncio.Lock()


def price_to_ticks(price: float) -> int:
    """
    Convert a UI decimal price to integer ticks.

    Example:
        100.25 -> 10025
    """

    return int(round(price * 100))


def ticks_to_price(ticks: int) -> float:
    return ticks / 100.0


# ================================================================
# UI book projection
# ================================================================

def build_snapshot():
    """
    Build a visualization snapshot.

    This does NOT perform matching.

    The C++ OrderBook remains the source of truth.
    """

    bid_levels = {}
    ask_levels = {}

    for order in active_orders.values():

        if order["remaining"] <= 0:
            continue

        price = order["price"]
        quantity = order["remaining"]

        if order["side"] == "BUY":

            bid_levels[price] = (
                bid_levels.get(price, 0)
                + quantity
            )

        else:

            ask_levels[price] = (
                ask_levels.get(price, 0)
                + quantity
            )

    bids = [
        {
            "price": ticks_to_price(price),
            "quantity": quantity
        }
        for price, quantity
        in sorted(
            bid_levels.items(),
            reverse=True
        )[:10]
    ]

    asks = [
        {
            "price": ticks_to_price(price),
            "quantity": quantity
        }
        for price, quantity
        in sorted(
            ask_levels.items()
        )[:10]
    ]

    return {
        "bids": bids,
        "asks": asks,
        "trades": recent_trades[-20:]
    }


async def broadcast_snapshot():
    """
    Send current state to all browser clients.
    """

    snapshot = build_snapshot()

    message = json.dumps(
        {
            "type": "snapshot",
            "data": snapshot
        }
    )

    dead_clients = []

    for websocket in connected_clients:

        try:

            await websocket.send_text(
                message
            )

        except Exception:

            dead_clients.append(
                websocket
            )

    for websocket in dead_clients:

        connected_clients.discard(
            websocket
        )


# ================================================================
# C++ response handling
# ================================================================

async def handle_ack(order_id: int):

    order = pending_orders.pop(
        order_id,
        None
    )

    if order is None:
        return

    # The order may already have been fully filled
    # before the ACK was processed.
    if order["remaining"] > 0:

        active_orders[order_id] = order


async def handle_reject(order_id: int):

    pending_orders.pop(
        order_id,
        None
    )


async def handle_trade(
    buy_order_id: int,
    sell_order_id: int,
    price_ticks: int,
    quantity: int
):

    # ------------------------------------------------------------
    # Update buyer
    # ------------------------------------------------------------

    if buy_order_id in pending_orders:

        order = pending_orders[
            buy_order_id
        ]

        order["remaining"] -= quantity

        if order["remaining"] <= 0:

            order["remaining"] = 0

    elif buy_order_id in active_orders:

        order = active_orders[
            buy_order_id
        ]

        order["remaining"] -= quantity

        if order["remaining"] <= 0:

            del active_orders[
                buy_order_id
            ]

    # ------------------------------------------------------------
    # Update seller
    # ------------------------------------------------------------

    if sell_order_id in pending_orders:

        order = pending_orders[
            sell_order_id
        ]

        order["remaining"] -= quantity

        if order["remaining"] <= 0:

            order["remaining"] = 0

    elif sell_order_id in active_orders:

        order = active_orders[
            sell_order_id
        ]

        order["remaining"] -= quantity

        if order["remaining"] <= 0:

            del active_orders[
                sell_order_id
            ]

    # ------------------------------------------------------------
    # Add trade to UI
    # ------------------------------------------------------------

    recent_trades.append(
        {
            "price": ticks_to_price(
                price_ticks
            ),
            "quantity": quantity,
            "buy_id": buy_order_id,
            "sell_id": sell_order_id,
            "ts": time.time()
        }
    )

    # Keep memory bounded.
    if len(recent_trades) > 100:

        del recent_trades[:-100]


async def gateway_reader_loop():
    """
    Continuously read responses from the C++ gateway.

    Responses:
        ACK
        REJECT
        TRADE
    """

    while True:

        if gateway_reader is None:

            await connect_gateway()

            await asyncio.sleep(1)

            continue

        try:

            # ----------------------------------------------------
            # Every message starts with one byte type.
            # ----------------------------------------------------

            type_data = await gateway_reader.readexactly(
                1
            )

            message_type = type_data[0]

            # ----------------------------------------------------
            # ACK
            # ----------------------------------------------------

            if message_type == ACK:

                payload = await gateway_reader.readexactly(
                    ACK_SIZE - 1
                )

                _, order_id = struct.unpack(
                    ACK_FORMAT,
                    type_data + payload
                )

                async with state_lock:

                    await handle_ack(
                        order_id
                    )

                await broadcast_snapshot()

            # ----------------------------------------------------
            # REJECT
            # ----------------------------------------------------

            elif message_type == REJECT:

                payload = await gateway_reader.readexactly(
                    REJECT_SIZE - 1
                )

                _, order_id, reason = struct.unpack(
                    REJECT_FORMAT,
                    type_data + payload
                )

                print(
                    "C++ rejected order",
                    order_id,
                    "reason",
                    reason
                )

                async with state_lock:

                    await handle_reject(
                        order_id
                    )

                await broadcast_snapshot()

            # ----------------------------------------------------
            # TRADE
            # ----------------------------------------------------

            elif message_type == TRADE:

                payload = await gateway_reader.readexactly(
                    TRADE_SIZE - 1
                )

                (
                    _,
                    buy_order_id,
                    sell_order_id,
                    price,
                    quantity
                ) = struct.unpack(
                    TRADE_FORMAT,
                    type_data + payload
                )

                print(
                    "Trade received:",
                    buy_order_id,
                    sell_order_id,
                    price,
                    quantity
                )

                async with state_lock:

                    await handle_trade(
                        buy_order_id,
                        sell_order_id,
                        price,
                        quantity
                    )

                await broadcast_snapshot()

            else:

                print(
                    "Unknown C++ message type:",
                    message_type
                )

        except (
            asyncio.IncompleteReadError,
            ConnectionError,
            OSError
        ):

            print(
                "C++ gateway connection lost"
            )

            await disconnect_gateway()

            await asyncio.sleep(1)

        except Exception as exc:

            print(
                "Gateway reader error:",
                exc
            )

            await disconnect_gateway()

            await asyncio.sleep(1)


# ================================================================
# Send order to C++
# ================================================================

async def send_order(
    side: str,
    price: float,
    quantity: int
):

    global next_order_id
    global next_sequence

    if gateway_writer is None:

        connected = await connect_gateway()

        if not connected:

            return None

    async with state_lock:

        order_id = next_order_id

        next_order_id += 1

        sequence = next_sequence

        next_sequence += 1

        side_code = (
            0
            if side == "BUY"
            else 1
        )

        price_ticks = price_to_ticks(
            price
        )

        message = struct.pack(
            NEW_ORDER_FORMAT,
            NEW_ORDER,
            sequence,
            order_id,
            side_code,
            price_ticks,
            int(quantity)
        )

        pending_orders[
            order_id
        ] = {
            "id": order_id,
            "side": side,
            "price": price_ticks,
            "quantity": int(quantity),
            "remaining": int(quantity)
        }

    try:

        gateway_writer.write(
            message
        )

        await gateway_writer.drain()

        print(
            "Sent order to C++:",
            order_id,
            side,
            price,
            quantity
        )

        return order_id

    except (
        ConnectionError,
        OSError
    ):

        await disconnect_gateway()

        async with state_lock:

            pending_orders.pop(
                order_id,
                None
            )

        return None


# ================================================================
# HTTP routes
# ================================================================

@app.get(
    "/",
    response_class=HTMLResponse
)
async def index():

    return (
        STATIC_DIR /
        "index.html"
    ).read_text()


@app.get("/health")
async def health():

    return {
        "status": "ok",
        "cpp_gateway": (
            gateway_writer is not None
        )
    }


# ================================================================
# WebSocket
# ================================================================

@app.websocket("/ws")
async def websocket_endpoint(
    websocket: WebSocket
):

    await websocket.accept()

    connected_clients.add(
        websocket
    )

    # Send current state immediately.
    await websocket.send_text(
        json.dumps(
            {
                "type": "snapshot",
                "data": build_snapshot()
            }
        )
    )

    try:

        while True:

            raw = await websocket.receive_text()

            message = json.loads(raw)

            if (
                message.get("action")
                == "place_order"
            ):

                side = message.get(
                    "side"
                )

                price = float(
                    message.get("price")
                )

                quantity = int(
                    message.get("quantity")
                )

                if side not in (
                    "BUY",
                    "SELL"
                ):

                    await websocket.send_text(
                        json.dumps(
                            {
                                "type": "error",
                                "message":
                                    "Invalid side"
                            }
                        )
                    )

                    continue

                if price <= 0:

                    await websocket.send_text(
                        json.dumps(
                            {
                                "type": "error",
                                "message":
                                    "Price must be positive"
                            }
                        )
                    )

                    continue

                if quantity <= 0:

                    await websocket.send_text(
                        json.dumps(
                            {
                                "type": "error",
                                "message":
                                    "Quantity must be positive"
                            }
                        )
                    )

                    continue

                order_id = await send_order(
                    side,
                    price,
                    quantity
                )

                if order_id is None:

                    await websocket.send_text(
                        json.dumps(
                            {
                                "type": "error",
                                "message":
                                    "C++ gateway unavailable"
                            }
                        )
                    )

                else:

                    await websocket.send_text(
                        json.dumps(
                            {
                                "type":
                                    "order_sent",
                                "order_id":
                                    order_id
                            }
                        )
                    )

    except WebSocketDisconnect:

        connected_clients.discard(
            websocket
        )

    except Exception:

        connected_clients.discard(
            websocket
        )


# ================================================================
# Startup
# ================================================================

@app.on_event("startup")
async def startup():

    asyncio.create_task(
        gateway_reader_loop()
    )

    print(
        "FastAPI web layer started"
    )


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        app,
        host="0.0.0.0",
        port=8000
    )