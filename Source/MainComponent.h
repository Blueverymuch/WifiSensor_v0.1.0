#pragma once
#include <JuceHeader.h>
#include "CustomScopeComponent.h" // 이건 기존 스코프 미터기용 컴포넌트
#include "ApScannerComponent.h" // 이건 또 추가된 컴포넌트임

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void onPingUpdatedFromJava (int tx, int rx, int loss, float rttMs); //핑모니터링 위해 추가...
    void paint (juce::Graphics&) override;
    void resized() override;

    void pushWifiDbmFromJni (float rssiDbm);
    void pushWifiScanFromJni (const juce::String& connSsid,
                              const juce::String& connBssid,
                              const juce::String& payload);
    void pushPingStatsFromJni (int txCount, int rxCount, int lossCount, float lastRttMs);

    static MainComponent* getInstance() noexcept { return instance; }

private:
    void timerCallback() override;
    void toggleWalkTest();
    void finalizeWalkTestAnalysis();

    void updateLiveRecommendation();

    void sendRoamingToAndroidStartUp (int triggerIdx, int deltaIdx, bool highPerf);
    void sendHighPerfToAndroidStartUp (bool highPerf);
    void controlAndroidPing (bool start);

    ApScannerComponent scannerView;
    CustomScopeComponent scope;

    // 하단 통합 컨트롤 & 텔레메트리 UI
    juce::Label statusLabel;
    juce::Label pingMonitorLabel;
    juce::ToggleButton highPerfToggle { "HIGH PERF" };
    juce::TextButton walkTestBtn { "START WALK" };
    juce::TextButton applyRoamingBtn { "APPLY" };

    // Walk-Test 트래킹 누적 데이터
    bool isWalkTesting = false;
    bool hasWalkResult = false;
    float walkMinDbm = 0.0f;
    float walkMaxDbm = -120.0f;
    float walkWorst2ndApDbm = -100.0f;
    int walkRoamCount = 0;
    juce::String lastTrackedBssid;
    juce::StringArray walkUniqueBssids;
    juce::String walkDensityVerdict = "LIVE MONITOR";

    // 실시간 Ping 통계
    int pingTx = 0;
    int pingRx = 0;
    int pingLoss = 0;
    float pingRttMs = 0.0f;
    float pingMaxRttMs = 0.0f; // 로밍 순간 지연 스파이크 포착용

    // 안전형 추천 StartUp 인덱스
    int recTriggerIdx = 2;   // default: -70 dBm
    int recDeltaIdx   = 4;   // default: 10 dB
    int recTriggerDbm = -70;
    int recDeltaDb    = 10;
    int recPeriodSec = 10; //추가!
    float recentAvgDbm = -65.0f;
    std::vector<float> recentSamples;

    static MainComponent* instance;
    //static MainComponent* gMainComponentInstance = nullptr; //이것 도 추가 함

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};