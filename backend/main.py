import asyncio
import json
import time
import httpx # NEW IMPORT
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import websockets

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

COINBASE_WS_URL = "wss://ws-feed.exchange.coinbase.com"
COINBASE_REST_URL = "https://api.exchange.coinbase.com/products"

# NEW: Historical Data Endpoint
@app.get("/history/{ticker}")
async def get_history(ticker: str):
    # Fetch last 300 1-minute candles
    async with httpx.AsyncClient() as client:
        resp = await client.get(
            f"{COINBASE_REST_URL}/{ticker}/candles",
            params={"granularity": 60} # 60 seconds = 1 minute
        )
        data = resp.json()
        
        # Coinbase returns: [time, low, high, open, close, volume]
        # We transform it to our standard Object format:
        formatted_data = []
        for candle in data[:100]: # Limit to last 100 mins for speed
            formatted_data.append({
                "time": candle[0], # Unix Timestamp
                "open": candle[3],
                "high": candle[2],
                "low": candle[1],
                "close": candle[4]
            })
        
        # Reverse because Coinbase sends newest first, but charts need oldest first
        return formatted_data[::-1]

@app.websocket("/ws")
async def market_data_stream(websocket: WebSocket):
    await websocket.accept()
    
    subscribe_msg = {
        "type": "subscribe",
        "product_ids": ["BTC-USD", "ETH-USD", "SOL-USD"],
        "channels": ["ticker"]
    }

    try:
        async with websockets.connect(COINBASE_WS_URL) as coinbase_ws:
            await coinbase_ws.send(json.dumps(subscribe_msg))
            
            while True:
                raw_data = await coinbase_ws.recv()
                data = json.loads(raw_data)

                if data.get("type") == "ticker":
                    packet = {
                        "ticker": data["product_id"],
                        "price": float(data["price"]),
                        "timestamp": time.time()
                    }
                    await websocket.send_text(json.dumps(packet))
                    
    except WebSocketDisconnect:
        print("Frontend disconnected")
    except Exception as e:
        print(f"Stream Error: {e}")