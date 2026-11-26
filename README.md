# WifiSensor — JUCE & Android Wi-Fi Roaming Analyzer & Tuner

산업용 안드로이드 단말기(엠쓰리모바일) 의 실시간 Wi-Fi 신호 환경을 분석해 최적의 로밍 파라미터(Trigger, Delta, Period, Sleep Policy)를 산출하고, 시스템 서비스 브로드캐스트를 통해 원터치로 적용하는 하이브리드(C++ JUCE + Android Java) 유틸리티 앱입니다.
Using M3 MOBILE's PDA, I made WIFI Roaming Optimization APP.


## Architecture & Control Flow

```mermaid
flowchart TD
    subgraph Android["Android Layer (Java / MainActivity)"]
        A[WifiManager / ScanResults] -->|RSSI, BSSID, Freq| B(JNI Bridge)
        G[Handler.postDelayed Queue] -->|0ms: Trigger Index| H[StartUp System Service]
        G -->|300ms: Delta Index| H
        G -->|600ms: Period Reverse-Index| H
        G -->|900ms: Sleep Policy + WifiLock| H
    end

    subgraph JUCE["Native Engine & UI Layer (C++ / JUCE)"]
        B --> C[RF & Signal Analyzer]
        P[Background Ping Probe / Gateway & DNS] -->|RTT Latency, Jitter, Loss| C
        C -->|Diagnose Roaming Bottleneck| D[JUCE Custom UI Dashboard]
        D -->|User Clicks 'Apply'| E[Parameter Mapper & 5s Snapper]
        E -->|JNI Callback| G
    end

    H -->|Driver / HAL Update| I((Stable Wi-Fi Roaming))