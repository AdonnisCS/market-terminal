import asyncio
import websockets
import json
import sys

async def monitor_feed():
    uri = "ws://127.0.0.1:8000/ws"
    print(f"Connecting to Terminal Relay at {uri}...")

    async with websockets.connect(uri) as websocket:
        print("Connected. Streaming live COINBASE data...\n")
        
        try:
            while True:
                msg = await websocket.recv()
                data = json.loads(msg)
                
                # Dynamic Print: Overwrites the current line
                sys.stdout.write(f"\r >> {data['ticker']:<8} | ${data['price']:,.2f}   ")
                sys.stdout.flush()
                
        except KeyboardInterrupt:
            print("\nStopped.")

if __name__ == "__main__":
    try:
        asyncio.run(monitor_feed())
    except KeyboardInterrupt:
        pass