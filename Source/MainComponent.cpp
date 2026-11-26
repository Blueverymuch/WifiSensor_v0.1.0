#include "MainComponent.h"

#if JUCE_ANDROID
#include <jni.h>
static JavaVM* gJvm = nullptr;
static jobject gActivityObj = nullptr;
static MainComponent* gMainComponentInstance = nullptr;

static JNIEnv* getAttachedJniEnv (bool& didAttach)
{
    didAttach = false;
    if (gJvm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    jint res = gJvm->GetEnv ((void**) &env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED)
    {
        if (gJvm->AttachCurrentThread (&env, nullptr) == JNI_OK)
            didAttach = true;
    }
    return env;
}
// 위 헬퍼 내용 추가됨

#endif

MainComponent* MainComponent::instance = nullptr;

MainComponent::MainComponent()
{
    instance = this;

    addAndMakeVisible (pingMonitorLabel);
    pingMonitorLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    pingMonitorLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0f172a));
    pingMonitorLabel.setColour (juce::Label::textColourId, juce::Colour (0xff34d399));
    pingMonitorLabel.setJustificationType (juce::Justification::centred);
    pingMonitorLabel.setText ("PING IDLE | Ready for Walk-Test", juce::dontSendNotification);
    // 핑모너터링

    addAndMakeVisible (scannerView);
    addAndMakeVisible (scope);

    statusLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff38bdf8));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText ("Ready. Tap START WALK to trace AP spacing & ping loss.", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

// High Performance Mode UI 토글 스위치 (Android 권한 정책상 수동 설정 안내)
    highPerfToggle.setToggleState (false, juce::dontSendNotification);
    highPerfToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xff64748b)); // 회색 비활성 텍스트
    highPerfToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff64748b));
    highPerfToggle.setAlpha (0.6f);

    highPerfToggle.onClick = [this]()
    {
        highPerfToggle.setToggleState (false, juce::dontSendNotification);

#if JUCE_ANDROID
        bool didAttach = false;
        if (JNIEnv* env = getAttachedJniEnv (didAttach))
        {
            if (gActivityObj != nullptr)
            {
                jclass cls = env->GetObjectClass (gActivityObj);
                jmethodID mid = env->GetMethodID (cls, "showWifiHighPerfGuideDialog", "()V");
                if (mid != nullptr)
                    env->CallVoidMethod (gActivityObj, mid);
                env->DeleteLocalRef (cls);
            }
            if (didAttach && gJvm != nullptr)
                gJvm->DetachCurrentThread();
        }
#endif
    };

    addAndMakeVisible (highPerfToggle);

    // Walk-Test 시작 / 종료 버튼
    walkTestBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff059669));
    walkTestBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    walkTestBtn.onClick = [this]() { toggleWalkTest(); };
    addAndMakeVisible (walkTestBtn);

    // StartUp 원터치 적용 버튼
    applyRoamingBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0284c7));
    applyRoamingBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    applyRoamingBtn.onClick = [this]()
    {
        sendRoamingToAndroidStartUp (recTriggerIdx, recDeltaIdx, highPerfToggle.getToggleState());
    };
    addAndMakeVisible (applyRoamingBtn);

    setSize (800, 480);
    startTimerHz (2);
}

MainComponent::~MainComponent()
{
    stopTimer();
    controlAndroidPing (false);
    if (instance == this)
        instance = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d1117));

    int barH = juce::jmax (74, (int) (getHeight() * 0.20f));
    auto bottomBar = getLocalBounds().removeFromBottom (barH);

    g.setColour (juce::Colour (0xff131922));
    g.fillRect (bottomBar);
    g.setColour (juce::Colour (0xff26303d));
    g.drawLine (0.0f, (float) bottomBar.getY(), (float) getWidth(), (float) bottomBar.getY(), 1.2f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    int totalH = area.getHeight();

    // 1. 하단 통합 컨트롤 바 (20%, 최소 74px)
    int bottomH = juce::jmax (74, (int) (totalH * 0.20f));
    // 안드로이드 하단 네비게이션 바 영역만큼 먼저 여백 확보 (추가)
    area.removeFromBottom (48);
    auto bottomBar = area.removeFromBottom (bottomH).reduced (8, 6);
    pingMonitorLabel.setBounds (area.removeFromBottom (26).reduced (8, 2));
    // ping 모니터링.. 라벨

    // Android 상단 상태바(노티) 및 하단 네비게이션 바 영역 제외
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = display->safeAreaInsets.subtractedFrom (area);

    // 이후 모든 UI 컴포넌트 배치는 getLocalBounds() 대신 이 'area'를 기준으로 배치!
    // 예: highPerfToggle.setBounds (area.removeFromTop (40));

    // 2. 상단 AP 스캐너 (45%)
    scannerView.setBounds (area.removeFromTop ((int) (totalH * 0.45f)));

    // 3. 중단 실시간 스코프 (35%)
    scope.setBounds (area);

    // 하단 바 우측 컨트롤 그룹 배치: [HIGH PERF 스위치] + [WALK 버튼] + [APPLY 버튼]
    auto rightControls = bottomBar.removeFromRight (330);
    applyRoamingBtn.setBounds (rightControls.removeFromRight (95));
    rightControls.removeFromRight (6);
    walkTestBtn.setBounds (rightControls.removeFromRight (115));
    rightControls.removeFromRight (6);
    highPerfToggle.setBounds (rightControls);

    bottomBar.removeFromRight (8);
    statusLabel.setBounds (bottomBar);
}

void MainComponent::toggleWalkTest()
{
    if (!isWalkTesting)
    {
        // Walk-Test 시작: 누적 통계 리셋 & Ping 개시
        isWalkTesting = true;
        hasWalkResult = false;
        walkMinDbm = 0.0f;
        walkMaxDbm = -120.0f;
        walkWorst2ndApDbm = -100.0f;
        walkRoamCount = 0;
        lastTrackedBssid.clear();
        walkUniqueBssids.clear();
        walkDensityVerdict = "TRACING...";

        pingTx = pingRx = pingLoss = 0;
        pingRttMs = 0.0f;
        pingMaxRttMs = 0.0f; // 추가: 이전 Walk-Test의 Peak RTT 초기화

        walkTestBtn.setButtonText ("STOP & ANALYZE");
        walkTestBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffdc2626));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xfffbbf24));

        controlAndroidPing (true);
    }
    else
    {
        // Walk-Test 종료: Ping 중지 & AP 간격/로밍 최종 분석
        isWalkTesting = false;
        hasWalkResult = true;
        controlAndroidPing (false);

        walkTestBtn.setButtonText ("START WALK");
        walkTestBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff059669));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff38bdf8));

        finalizeWalkTestAnalysis();
    }
}

void MainComponent::finalizeWalkTestAnalysis()
{
    int uniqueAps = juce::jmax (1, walkUniqueBssids.size());
    float minVal = (walkMinDbm < -1.0f) ? walkMinDbm : recentAvgDbm;

    // Trigger Index: 0=-80, 1=-75, 2=-70, 3=-65, 4=-60
    // Delta Index:   0=30dB, 1=25dB, 2=20dB, 3=15dB, 4=10dB, 5=5dB, 6=0dB
    recPeriodSec = 10;
    juce::String pingVerdict = "PASS (Zero-Loss)";

    // 1. AP 밀집도 & 감도 기반 1차 프로파일
    if (uniqueAps >= 3 && minVal >= -68.0f && walkWorst2ndApDbm >= -68.0f)
    {
        walkDensityVerdict = "DENSE (Anti-PingPong)";
        recTriggerIdx = 3; recTriggerDbm = -65;
        recDeltaIdx   = 3; recDeltaDb    = 15;
        recPeriodSec  = 15;
    }
    else if (uniqueAps >= 2 && minVal < -72.0f)
    {
        walkDensityVerdict = "SPARSE / EDGE (Early Handover)";
        recTriggerIdx = 3; recTriggerDbm = -65;
        recDeltaIdx   = 5; recDeltaDb    = 5;
        recPeriodSec  = 5;
    }
    else if (uniqueAps >= 2)
    {
        walkDensityVerdict = "STANDARD (Balanced)";
        recTriggerIdx = 2; recTriggerDbm = -70;
        recDeltaIdx   = 4; recDeltaDb    = 10;
        recPeriodSec  = 10;
    }
    else
    {
        walkDensityVerdict = "SINGLE AP (Scan Save)";
        recTriggerIdx = 1; recTriggerDbm = -75;
        recDeltaIdx   = 3; recDeltaDb    = 15;
        recPeriodSec  = 20;
    }

    // 2. Gateway Ping 실측 기반 2차 보정 (스캔 지터 vs 실제 로밍 단절 분리 판정)
    if (pingTx > 0)
    {
        if (walkRoamCount == 0 && minVal >= -68.0f && (pingLoss >= 1 || pingMaxRttMs >= 90.0f))
        {
            // 신호가 양호하고 로밍도 없었는데 핑만 튄 경우 = 과도한 오프채널 스캔 지터!
            recTriggerIdx = 1; recTriggerDbm = -75;
            recDeltaIdx   = 3; recDeltaDb    = 15;
            recPeriodSec  = 20;
            pingVerdict   = "SCAN JITTER -> Suppress Off-Channel Scan";
        }
        else if (pingLoss >= 2 || pingMaxRttMs >= 150.0f)
        {
            // 경계 이동 중 실제 단절/스파이크 발생 -> 최고속 선제 로밍
            recTriggerIdx = 4; recTriggerDbm = -60;
            recDeltaIdx   = (pingLoss >= 3) ? 6 : 5;
            recDeltaDb    = (pingLoss >= 3) ? 0 : 5;
            recPeriodSec  = 3;
            pingVerdict   = "CRITICAL LOSS -> Fast Roam (-60dBm / 3s)";
        }
        else if (pingLoss == 1 || pingMaxRttMs >= 90.0f)
        {
            // 경미한 지연 -> Period와 Delta만 축소
            recTriggerIdx = juce::jmax (recTriggerIdx, 3);
            recTriggerDbm = -80 + (recTriggerIdx * 5);
            recDeltaIdx   = 5; recDeltaDb = 5;
            recPeriodSec  = juce::jmin (recPeriodSec, 5);
            pingVerdict   = "MINOR SPIKE -> Period 5s & Delta 5dB";
        }
    }

    updateLiveRecommendation();

    juce::String report = juce::String::formatted (
            "[Environment & Roaming]\n"
            "- Type: %s (Same-SSID APs: %d, Roams: %d)\n"
            "- RSSI Range: %.0f ~ %.0f dBm (2nd AP: %.0f dBm)\n\n"
            "[Gateway Ping Quality]\n"
            "- Diagnosis: %s\n"
            "- Packets: %d Rx / %d Tx (Loss: %d)\n"
            "- Last RTT: %.1f ms | Peak RTT: %.1f ms\n\n"
            "[Recommended StartUp Config]\n"
            "- Keep Wi-Fi On Sleep: ALWAYS\n"
            "- Roaming Trigger: %d dBm\n"
            "- Roaming Delta: %d dB\n"
            "- Roaming Period: %d sec",
            walkDensityVerdict.toRawUTF8(), uniqueAps, walkRoamCount,
            minVal, walkMaxDbm, walkWorst2ndApDbm,
            pingVerdict.toRawUTF8(),
            pingRx, pingTx, pingLoss,
            pingRttMs, pingMaxRttMs,
            recTriggerDbm, recDeltaDb, recPeriodSec
    );

#if JUCE_ANDROID
    bool didAttach = false;
    if (JNIEnv* env = getAttachedJniEnv (didAttach))
    {
        if (gActivityObj != nullptr)
        {
            jclass cls = env->GetObjectClass (gActivityObj);
            // report 문자열과 함께 Apply 버튼 실행용 int 3개(trigger, delta, period)를 직접 전달
            jmethodID mid = env->GetMethodID (cls, "showWalkAnalysisDialog", "(Ljava/lang/String;III)V");
            if (mid != nullptr)
            {
                jstring jReport = env->NewStringUTF (report.toRawUTF8());
                env->CallVoidMethod (gActivityObj, mid, jReport,
                                     (jint) recTriggerDbm, (jint) recDeltaDb, (jint) recPeriodSec);
                env->DeleteLocalRef (jReport);
            }
            env->DeleteLocalRef (cls);
        }
        if (didAttach && gJvm != nullptr)
            gJvm->DetachCurrentThread();
    }
#endif
}

void MainComponent::timerCallback()
{
    updateLiveRecommendation();
}

void MainComponent::updateLiveRecommendation()
{
    if (!recentSamples.empty())
    {
        float sum = 0.0f;
        for (float v : recentSamples) sum += v;
        recentAvgDbm = sum / (float) recentSamples.size();
    }

    if (!isWalkTesting && !hasWalkResult)
    {
        int sameCount = scannerView.getSameSsidCount();
        if (sameCount >= 3 && recentAvgDbm >= -65.0f)
        {
            recTriggerIdx = 3; recTriggerDbm = -65;
            recDeltaIdx   = 4; recDeltaDb    = 10;
        }
        else if (sameCount >= 2)
        {
            recTriggerIdx = 2; recTriggerDbm = -70;
            recDeltaIdx   = 4; recDeltaDb    = 10;
        }
        else
        {
            recTriggerIdx = 1; recTriggerDbm = -75;
            recDeltaIdx   = 3; recDeltaDb    = 15;
        }
    }

    juce::String minStr = (walkMinDbm < -1.0f) ? juce::String ((int) walkMinDbm) : juce::String ((int) recentAvgDbm);
    juce::String maxStr = (walkMaxDbm > -119.0f) ? juce::String ((int) walkMaxDbm) : juce::String ((int) recentAvgDbm);

    juce::String line1 = "[" + walkDensityVerdict + "]  Min/Max: " + minStr + "/" + maxStr + "dBm"
                         + " | APs:" + juce::String (juce::jmax (1, walkUniqueBssids.size()))
                         + " Roams:" + juce::String (walkRoamCount);

    juce::String pingPart = (pingTx > 0)
                            ? ("Ping Loss:" + juce::String (pingLoss) + "/" + juce::String (pingTx) + " (" + juce::String (pingRttMs, 0) + "ms)")
                            : "Ping: Standby";

    juce::String line2 = pingPart + "  ->  REC Trig:" + juce::String (recTriggerDbm)
                         + "dBm / Delta:" + juce::String (recDeltaDb) + "dB";

    statusLabel.setText (line1 + "\n" + line2, juce::dontSendNotification);
}

void MainComponent::pushWifiDbmFromJni (float rssiDbm)
{
    juce::MessageManager::callAsync ([this, rssiDbm]() {
        if (instance == nullptr) return;

        scope.pushDbmSample (rssiDbm);
        recentSamples.push_back (rssiDbm);
        if (recentSamples.size() > 100)
            recentSamples.erase (recentSamples.begin());

        if (isWalkTesting && rssiDbm < -10.0f && rssiDbm > -115.0f)
        {
            if (walkMinDbm > -1.0f || rssiDbm < walkMinDbm)
                walkMinDbm = rssiDbm;
            if (rssiDbm > walkMaxDbm)
                walkMaxDbm = rssiDbm;

            // 신호 약화 구간에서 2순위 후보 AP 신호 세기(중첩도) 기록
            float secondDbm = scannerView.getSecondBestSameSsidDbm();
            if (secondDbm > -99.0f && (walkWorst2ndApDbm < -99.0f || secondDbm > walkWorst2ndApDbm))
                walkWorst2ndApDbm = secondDbm;
        }
    });
}

void MainComponent::pushWifiScanFromJni (const juce::String& connSsid,
                                         const juce::String& connBssid,
                                         const juce::String& payload)
{
    juce::MessageManager::callAsync ([this, connSsid, connBssid, payload]() {
        if (instance == nullptr) return;

        scannerView.updateScanData (connSsid, connBssid, payload);

        if (isWalkTesting && connBssid.isNotEmpty() && connBssid != "00:00:00:00:00:00")
        {
            walkUniqueBssids.addIfNotAlreadyThere (connBssid);
            if (lastTrackedBssid.isNotEmpty() && lastTrackedBssid != connBssid)
                walkRoamCount++;
            lastTrackedBssid = connBssid;
        }
    });
}

void MainComponent::pushPingStatsFromJni (int txCount, int rxCount, int lossCount, float lastRttMs)
{
    juce::MessageManager::callAsync ([this, txCount, rxCount, lossCount, lastRttMs]() {
        if (instance == nullptr) return;
        pingTx = txCount;
        pingRx = rxCount;
        pingLoss = lossCount;
        pingRttMs = lastRttMs;
    });
}

void MainComponent::controlAndroidPing (bool start)
{
#if JUCE_ANDROID
    bool didAttach = false;
    if (JNIEnv* env = getAttachedJniEnv (didAttach); env != nullptr && gActivityObj != nullptr)
    {
        jclass cls = env->GetObjectClass (gActivityObj);
        jmethodID mid = env->GetMethodID (cls, start ? "startGatewayPing" : "stopGatewayPing", "()V");
        if (mid != nullptr)
            env->CallVoidMethod (gActivityObj, mid);
        env->DeleteLocalRef (cls);
        if (didAttach) gJvm->DetachCurrentThread();
    }
#else
    juce::ignoreUnused (start);
#endif //헬퍼 위해 교체함
}

void MainComponent::onPingUpdatedFromJava (int tx, int rx, int loss, float rttMs)
{
    juce::MessageManager::callAsync ([this, tx, rx, loss, rttMs]()
                                     {
                                         if (!isWalkTesting) return;

                                         pingTx = tx;
                                         pingRx = rx;
                                         pingLoss = loss;
                                         pingRttMs = rttMs;
                                         if (rttMs > pingMaxRttMs)
                                             pingMaxRttMs = rttMs;

                                         // Loss 발생 시 빨간색, 정상이면 민트색으로 즉시 시각 피드백
                                         pingMonitorLabel.setColour (juce::Label::textColourId,
                                                                     (pingLoss > 0) ? juce::Colour (0xfff87171) : juce::Colour (0xff34d399));

                                         pingMonitorLabel.setText (juce::String::formatted (
                                                                           "LIVE PING  Rx:%d / Tx:%d  |  RTT: %.1f ms (Peak %.1f)  |  Loss: %d",
                                                                           pingRx, pingTx, pingRttMs, pingMaxRttMs, pingLoss),
                                                                   juce::dontSendNotification);
                                     });
}


void MainComponent::sendHighPerfToAndroidStartUp (bool highPerf)
{
#if JUCE_ANDROID
    bool didAttach = false;
    if (JNIEnv* env = getAttachedJniEnv (didAttach); env != nullptr && gActivityObj != nullptr)
    {
        jclass cls = env->GetObjectClass (gActivityObj);
        jmethodID mid = env->GetMethodID (cls, "setHighPerfModeOnly", "(Z)V");
        if (mid != nullptr)
            env->CallVoidMethod (gActivityObj, mid, (jboolean) highPerf);
        env->DeleteLocalRef (cls);
        if (didAttach) gJvm->DetachCurrentThread();
    }
#else
    juce::ignoreUnused (highPerf);
#endif
} //헬퍼 위해 교체함

void MainComponent::sendRoamingToAndroidStartUp (int triggerIdx, int deltaIdx, bool highPerf)
{
    juce::ignoreUnused (triggerIdx, deltaIdx, highPerf);
#if JUCE_ANDROID
    bool didAttach = false;
    if (JNIEnv* env = getAttachedJniEnv (didAttach); env != nullptr && gActivityObj != nullptr)
    {
        jclass cls = env->GetObjectClass (gActivityObj);
        jmethodID mid = env->GetMethodID (cls, "applyStartUpRoamingConfig", "(III)V");
        if (mid != nullptr)
            env->CallVoidMethod (gActivityObj, mid,
                                 (jint) recTriggerDbm, (jint) recDeltaDb, (jint) recPeriodSec);
        env->DeleteLocalRef (cls);
        if (didAttach) gJvm->DetachCurrentThread();
    }
#endif
}




#if JUCE_ANDROID
extern "C" JNIEXPORT void JNICALL
Java_com_yourcompany_wifisensor_MainActivity_nativeRegisterActivity (JNIEnv* env, jobject thiz)
{
    env->GetJavaVM (&gJvm);
    if (gActivityObj != nullptr)
        env->DeleteGlobalRef (gActivityObj);
    gActivityObj = env->NewGlobalRef (thiz);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yourcompany_wifisensor_MainActivity_nativeOnWifiRssiUpdated (JNIEnv*, jobject, jfloat rssiDbm)
{
    if (auto* mc = MainComponent::getInstance())
        mc->pushWifiDbmFromJni (rssiDbm);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yourcompany_wifisensor_MainActivity_nativeOnWifiScanUpdated (JNIEnv* env, jobject,
                                                                      jstring jConnSsid,
                                                                      jstring jConnBssid,
                                                                      jstring jPayload)
{
    if (auto* mc = MainComponent::getInstance())
    {
        auto toJuceStr = [env] (jstring js) -> juce::String {
            if (js == nullptr) return {};
            const char* utf = env->GetStringUTFChars (js, nullptr);
            juce::String res = juce::String::fromUTF8 (utf);
            env->ReleaseStringUTFChars (js, utf);
            return res;
        };

        mc->pushWifiScanFromJni (toJuceStr (jConnSsid),
                                 toJuceStr (jConnBssid),
                                 toJuceStr (jPayload));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_yourcompany_wifisensor_MainActivity_nativeOnPingUpdated (JNIEnv*, jobject,
                                                                  jint txCount, jint rxCount,
                                                                  jint lossCount, jfloat lastRttMs)
{
    if (auto* mc = MainComponent::getInstance())
    {
        mc->pushPingStatsFromJni ((int) txCount, (int) rxCount, (int) lossCount, (float) lastRttMs);
        mc->onPingUpdatedFromJava ((int) txCount, (int) rxCount, (int) lossCount, (float) lastRttMs);
    }
}
#endif