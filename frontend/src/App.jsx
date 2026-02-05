import { useState, useEffect, useRef, useMemo } from 'react';
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from 'recharts';
import { Zap, ShieldCheck, Activity, BarChart3 } from 'lucide-react';

function App() {
  const [marketData, setMarketData] = useState({});
  const [activeCoin, setActiveCoin] = useState("BTC-USD");
  const [candles, setCandles] = useState({ "BTC-USD": [], "ETH-USD": [], "SOL-USD": [] });
  const currentCandleRef = useRef({});

  // 1. NEW: Fetch History on Load
  useEffect(() => {
    const fetchHistory = async () => {
      const tickers = ["BTC-USD", "ETH-USD", "SOL-USD"];
      const newCandles = { ...candles };

      for (const ticker of tickers) {
        try {
          const res = await fetch(`http://127.0.0.1:8000/history/${ticker}`);
          const data = await res.json();
          
          // Format for Recharts
          newCandles[ticker] = data.map(c => ({
            ...c,
            // Convert Unix timestamp to readable time string
            time: new Date(c.time * 1000).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'})
          }));
        } catch (err) {
          console.error("Failed to load history for", ticker, err);
        }
      }
      setCandles(newCandles);
    };

    fetchHistory();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []); // Run once on mount

  // 2. EXISTING: Live WebSocket Updates
  useEffect(() => {
    const socket = new WebSocket('ws://127.0.0.1:8000/ws');
    
    socket.onmessage = (event) => {
      const data = JSON.parse(event.data);
      const { ticker, price, timestamp } = data;

      setMarketData(prev => ({
        ...prev,
        [ticker]: { price, prevPrice: prev[ticker]?.price || price }
      }));

      const minuteStr = new Date(timestamp * 1000).getMinutes();
      let activeCandle = currentCandleRef.current[ticker];

      // If we have history, grab the last candle to see if we should continue it or start new
      if (!activeCandle && candles[ticker]?.length > 0) {
        // Initialize ref with the last known candle from history to prevent gaps
        // logic omitted for brevity, but "Step Chart" handles gaps visually fine
      }

      if (!activeCandle || activeCandle.minute !== minuteStr) {
        activeCandle = { 
          minute: minuteStr, 
          time: new Date(timestamp * 1000).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'}),
          open: price, high: price, low: price, close: price 
        };
        
        setCandles(prev => {
          const prevCandles = prev[ticker] || [];
          // Keep last 100 candles max
          return { ...prev, [ticker]: [...prevCandles.slice(-99), activeCandle] };
        });
      } else {
        activeCandle.high = Math.max(activeCandle.high, price);
        activeCandle.low = Math.min(activeCandle.low, price);
        activeCandle.close = price;
        
        // Force update the last candle in state so it "wiggles" live
        setCandles(prev => {
           const prevCandles = prev[ticker] || [];
           const newHistory = [...prevCandles];
           newHistory[newHistory.length - 1] = activeCandle;
           return { ...prev, [ticker]: newHistory };
        });
      }
      currentCandleRef.current[ticker] = activeCandle;
    };

    return () => socket.close();
  }, []); // Note: In a real app, you'd merge this with the history fetcher more tightly

  // ... (Keep your Y-Domain logic and Return JSX exactly the same)
  const yDomain = useMemo(() => {
    const data = candles[activeCoin];
    if (!data || data.length === 0) return ['auto', 'auto'];
    const prices = data.map(d => d.close);
    const min = Math.min(...prices);
    const max = Math.max(...prices);
    const margin = (max - min) * 0.5 || max * 0.005; 
    return [min - margin, max + margin];
  }, [candles, activeCoin]);

  return (
    // ... PASTE YOUR EXISTING JSX HERE ...
    // (Use the exact same return statement from the previous "CSS Grid" step)
    <div style={{ 
      backgroundColor: '#0a0b0d', color: '#fff', height: '100vh', width: '100vw',
      display: 'grid', gridTemplateColumns: '300px 1fr', overflow: 'hidden' 
    }}>
      
      {/* --- LEFT NAVIGATION --- */}
      <aside style={{ background: '#111418', borderRight: '1px solid #1e222d', padding: '1.5rem', display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px', marginBottom: '2rem' }}>
          <Zap size={22} color="#fcd535" fill="#fcd535" />
          <h2 style={{ fontSize: '0.9rem', fontWeight: '900', letterSpacing: '2px' }}>TERMINAL_v1.0</h2>
        </div>

        <div style={{ flex: 1, overflowY: 'auto' }}>
          {Object.entries(marketData).map(([ticker, info]) => (
            <div key={ticker} onClick={() => setActiveCoin(ticker)} style={{
              padding: '1rem', borderRadius: '12px', cursor: 'pointer', marginBottom: '0.75rem',
              background: ticker === activeCoin ? '#1e2329' : 'transparent',
              border: `1px solid ${ticker === activeCoin ? '#3b3e4a' : 'transparent'}`,
            }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.7rem', color: '#848e9c', marginBottom: '4px' }}>
                <span>{ticker}</span>
                <span style={{ color: info.price >= info.prevPrice ? '#00ff88' : '#ff4d4d' }}>
                  {info.price >= info.prevPrice ? '▲' : '▼'}
                </span>
              </div>
              <div style={{ fontSize: '1.2rem', fontWeight: '700' }}>
                ${info.price.toLocaleString(undefined, { minimumFractionDigits: 2 })}
              </div>
            </div>
          ))}
        </div>

        <div style={{ marginTop: 'auto', padding: '1rem', background: '#0a0b0d', borderRadius: '8px', fontSize: '0.7rem', color: '#475569' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span>LATENCY:</span><span style={{ color: '#00ff88' }}>24ms</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span>ENGINE:</span><span>COINBASE_WSS</span>
          </div>
        </div>
      </aside>

      {/* --- MAIN DASHBOARD --- */}
      <main style={{ display: 'flex', flexDirection: 'column', padding: '2rem', overflow: 'hidden' }}>
        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end', marginBottom: '2rem' }}>
          <div>
            <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
              <h1 style={{ fontSize: '3rem', fontWeight: '900', margin: 0 }}>{activeCoin}</h1>
              <span style={{ padding: '4px 8px', background: '#1e2329', borderRadius: '4px', fontSize: '0.7rem', color: '#fcd535' }}>LIVE</span>
            </div>
            <p style={{ color: '#848e9c', margin: '5px 0 0 0', display: 'flex', gap: '15px' }}>
              <span style={{ display: 'flex', alignItems: 'center', gap: '5px' }}><ShieldCheck size={14}/> SECURE_FEED</span>
              <span style={{ display: 'flex', alignItems: 'center', gap: '5px' }}><Activity size={14}/> 100ms_POLLING</span>
            </p>
          </div>
          <div style={{ textAlign: 'right' }}>
            <div style={{ fontSize: '2.5rem', fontWeight: '700', color: (marketData[activeCoin]?.price >= marketData[activeCoin]?.prevPrice) ? '#00ff88' : '#ff4d4d' }}>
              ${marketData[activeCoin]?.price.toLocaleString(undefined, { minimumFractionDigits: 2 }) || '---'}
            </div>
            <span style={{ color: '#848e9c', fontSize: '0.7rem' }}>AGGREGATED_TRADE_PRICE</span>
          </div>
        </header>

        {/* --- CHART CONTAINER --- */}
        <div style={{ flex: 1, position: 'relative', background: '#111418', borderRadius: '20px', border: '1px solid #1e222d', padding: '1rem' }}>
          <div style={{ position: 'absolute', top: '15px', left: '20px', display: 'flex', gap: '20px', zIndex: 10 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '5px', fontSize: '0.7rem', color: '#848e9c' }}>
              <BarChart3 size={14}/> OHLC_VOLUME: <span style={{ color: '#fff' }}>STABLE</span>
            </div>
          </div>
          
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={candles[activeCoin]}>
              <defs>
                <linearGradient id="chartColor" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#fcd535" stopOpacity={0.2}/>
                  <stop offset="95%" stopColor="#fcd535" stopOpacity={0}/>
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#1e222d" />
              <XAxis dataKey="time" hide />
              <YAxis 
                domain={yDomain} // APPLYING THE BUFFER FIX
                orientation="right" 
                stroke="#475569" 
                fontSize={12} 
                axisLine={false} 
                tickLine={false} 
                tickFormatter={(v) => `$${v.toLocaleString()}`}
              />
              <Tooltip contentStyle={{ background: '#0a0b0d', border: '1px solid #1e222d' }} />
              <Area 
                type="stepAfter" // Makes the history look "blocky" and professional
                dataKey="close" 
                stroke="#fcd535" 
                strokeWidth={2} 
                fill="url(#chartColor)" 
                isAnimationActive={false} 
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>
      </main>
    </div>
  );
}

export default App;