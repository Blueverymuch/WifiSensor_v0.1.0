#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>
#include <algorithm>

struct ApChannelInfo
{
    juce::String ssid;
    juce::String bssid;
    int frequency = 0;
    float targetDbm = -100.0f;
    float smoothedDbm = -100.0f;
    float peakDbm = -100.0f;
    juce::int64 lastSeenMs = 0;
};

class ApScannerComponent : public juce::Component,
                           private juce::Timer
{
public:
    ApScannerComponent()
    {
        startTimerHz (30); // 30fps 부드러운 게이지 보간
    }

    ~ApScannerComponent() override
    {
        stopTimer();
    }

    void updateScanData (const juce::String& connSsid,
                         const juce::String& connBssid,
                         const juce::String& payload)
    {
        connectedSsid = connSsid;
        connectedBssid = connBssid.toLowerCase();
        auto now = juce::Time::currentTimeMillis();

        auto lines = juce::StringArray::fromLines (payload);
        for (const auto& line : lines)
        {
            if (line.isEmpty()) continue;
            auto tokens = juce::StringArray::fromTokens (line, "|", "");
            if (tokens.size() < 4) continue;

            juce::String ssid  = tokens[0].isEmpty() ? "(Hidden)" : tokens[0];
            juce::String bssid = tokens[1].toLowerCase();
            float rssi         = tokens[2].getFloatValue();
            int freq           = tokens[3].getIntValue();

            auto& ap = apMap[bssid];
            bool isNew = (ap.bssid.isEmpty());
            ap.ssid = ssid;
            ap.bssid = bssid;
            ap.frequency = freq;
            ap.targetDbm = juce::jlimit (-100.0f, -20.0f, rssi);
            ap.lastSeenMs = now;

            if (isNew)
            {
                ap.smoothedDbm = ap.targetDbm;
                ap.peakDbm = ap.targetDbm;
            }
            else if (ap.targetDbm > ap.peakDbm)
            {
                ap.peakDbm = ap.targetDbm;
            }
        }
    }

    int getSameSsidCount() const { return sameSsidCount; }
    juce::String getTargetSsid() const { return activeTargetSsid; }

    float getSecondBestSameSsidDbm() const // 이하 추가된 구조체는 최적값 찾기 도구
    {
        int found = 0;
        for (const auto& ap : sortedAps)
        {
            if (activeTargetSsid.isNotEmpty() && ap.ssid == activeTargetSsid)
            {
                ++found;
                if (found == 2) return ap.smoothedDbm;
            }
        }
        return -100.0f;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.fillAll (juce::Colour (0xff101318));

        // 외곽 테두리
        g.setColour (juce::Colour (0xff26303d));
        g.drawRect (bounds, 1.0f);

        // 상단 헤더 바 (Target SSID & 동일 SSID 개수 카운터)
        auto headerArea = bounds.removeFromTop (30.0f).reduced (10.0f, 2.0f);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));

        juce::String titleText = "TARGET SSID: " + (activeTargetSsid.isEmpty() ? "SCANNING..." : activeTargetSsid)
                                 + "   |   SAME-SSID APs: " + juce::String (sameSsidCount)
                                 + "   (TOTAL: " + juce::String ((int) sortedAps.size()) + ")";
        g.setColour (juce::Colour (0xff38bdf8));
        g.drawText (titleText, headerArea, juce::Justification::centredLeft, true);

        if (sortedAps.empty())
        {
            g.setColour (juce::Colours::grey);
            g.drawText ("Waiting for Wi-Fi scan results...", bounds, juce::Justification::centred);
            return;
        }

        // 게이지 영역
        auto meterArea = bounds.reduced (10.0f, 6.0f);
        int maxSlots = juce::jmin (12, (int) sortedAps.size()); // 최대 12개 슬롯 동시 표시
        float slotGap = 6.0f;
        float slotWidth = (meterArea.getWidth() - slotGap * (maxSlots - 1)) / (float) maxSlots;

        for (int i = 0; i < maxSlots; ++i)
        {
            const auto& ap = sortedAps[(size_t) i];
            bool isConnected = (connectedBssid.isNotEmpty() && ap.bssid == connectedBssid);
            bool isSameSsid  = (activeTargetSsid.isNotEmpty() && ap.ssid == activeTargetSsid);

            juce::Rectangle<float> slotRect (
                    meterArea.getX() + i * (slotWidth + slotGap),
                    meterArea.getY(),
                    slotWidth,
                    meterArea.getHeight()
            );

            // 슬롯 배경
            g.setColour (isConnected ? juce::Colour (0xff182838)
                                     : (isSameSsid ? juce::Colour (0xff161d26) : juce::Colour (0xff12151a)));
            g.fillRoundedRectangle (slotRect, 4.0f);

            // 하단 라벨 영역 (채널 + BSSID 끝 5자리 + SSID)
            auto labelArea = slotRect.removeFromBottom (44.0f).reduced (2.0f);
            auto topValueArea = slotRect.removeFromTop (18.0f);
            auto barArea = slotRect.reduced (6.0f, 2.0f);

            // dBm 수치 텍스트
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.setColour (isSameSsid ? juce::Colours::white : juce::Colours::grey);
            g.drawText (juce::String ((int) std::round (ap.smoothedDbm)) + "dB",
                        topValueArea, juce::Justification::centred, false);

            // 게이지 트랙 배경
            g.setColour (juce::Colour (0xff0a0c10));
            g.fillRoundedRectangle (barArea, 3.0f);

            // dBm (-95 ~ -30) -> 0.0 ~ 1.0 정규화
            float norm = juce::jlimit (0.02f, 1.0f, (ap.smoothedDbm + 95.0f) / 65.0f);
            float fillH = barArea.getHeight() * norm;
            auto fillRect = barArea.withTop (barArea.getBottom() - fillH);

            // 신호 세기별 컬러
            juce::Colour barCol = (ap.smoothedDbm >= -60.0f) ? juce::Colour (0xff22c55e)
                                                             : (ap.smoothedDbm >= -72.0f) ? juce::Colour (0xffeab308)
                                                                                          : juce::Colour (0xffef4444);
            if (!isSameSsid)
                barCol = barCol.withAlpha (0.35f);

            g.setColour (barCol);
            g.fillRoundedRectangle (fillRect, 3.0f);

            // 피크 홀드 라인
            float peakNorm = juce::jlimit (0.02f, 1.0f, (ap.peakDbm + 95.0f) / 65.0f);
            float peakY = barArea.getBottom() - barArea.getHeight() * peakNorm;
            g.setColour (isSameSsid ? juce::Colours::white.withAlpha (0.85f) : juce::Colours::grey.withAlpha (0.4f));
            g.drawHorizontalLine ((int) peakY, barArea.getX(), barArea.getRight());

            // 하단 텍스트 (채널 / BSSID 끝자리 / SSID)
            int ch = freqToChannel (ap.frequency);
            juce::String bandTag = (ap.frequency >= 5925) ? "6G" : (ap.frequency >= 5000 ? "5G" : "2G");
            juce::String shortMac = ap.bssid.substring (juce::jmax (0, ap.bssid.length() - 5)).toUpperCase();

            g.setFont (juce::FontOptions (10.0f, isConnected ? juce::Font::bold : juce::Font::plain));
            g.setColour (isConnected ? juce::Colour (0xff38bdf8)
                                     : (isSameSsid ? juce::Colours::lightgrey : juce::Colours::dimgrey));

            auto row1 = labelArea.removeFromTop (14.0f);
            auto row2 = labelArea.removeFromTop (14.0f);
            auto row3 = labelArea;

            g.drawText (isConnected ? "[CONN] CH" + juce::String (ch) : bandTag + " CH" + juce::String (ch),
                        row1, juce::Justification::centred, true);
            g.drawText (shortMac, row2, juce::Justification::centred, true);
            g.drawText (ap.ssid, row3, juce::Justification::centred, true);

            // 현재 연결된 AP 외곽 하이라이트 테두리
            if (isConnected)
            {
                g.setColour (juce::Colour (0xff38bdf8));
                g.drawRoundedRectangle (slotRect.withBottom (labelArea.getBottom()), 4.0f, 1.8f);
            }
        }
    }

private:
    void timerCallback() override
    {
        auto now = juce::Time::currentTimeMillis();

        // 15초 이상 안 잡힌 AP 정리 & 게이지 보간
        for (auto it = apMap.begin(); it != apMap.end(); )
        {
            if (now - it->second.lastSeenMs > 15000)
            {
                it = apMap.erase (it);
            }
            else
            {
                it->second.smoothedDbm += (it->second.targetDbm - it->second.smoothedDbm) * 0.22f;
                it->second.peakDbm = juce::jmax (it->second.smoothedDbm, it->second.peakDbm - 0.15f);
                ++it;
            }
        }

        // 기준 Target SSID 결정 (연결 중이면 연결된 SSID, 미연결이면 가장 강한 AP의 SSID)
        if (connectedSsid.isNotEmpty())
        {
            activeTargetSsid = connectedSsid;
        }
        else if (!apMap.empty())
        {
            float best = -200.0f;
            for (const auto& kv : apMap)
            {
                if (kv.second.targetDbm > best && kv.second.ssid != "(Hidden)")
                {
                    best = kv.second.targetDbm;
                    activeTargetSsid = kv.second.ssid;
                }
            }
        }

        // 정렬: 1순위 연결된 AP -> 2순위 동일 SSID (신호 강한 순) -> 3순위 타 SSID (신호 강한 순)
        sortedAps.clear();
        sameSsidCount = 0;
        for (const auto& kv : apMap)
        {
            sortedAps.push_back (kv.second);
            if (activeTargetSsid.isNotEmpty() && kv.second.ssid == activeTargetSsid)
                ++sameSsidCount;
        }

        std::sort (sortedAps.begin(), sortedAps.end(), [this] (const ApChannelInfo& a, const ApChannelInfo& b)
        {
            bool aConn = (connectedBssid.isNotEmpty() && a.bssid == connectedBssid);
            bool bConn = (connectedBssid.isNotEmpty() && b.bssid == connectedBssid);
            if (aConn != bConn) return aConn;

            bool aSame = (activeTargetSsid.isNotEmpty() && a.ssid == activeTargetSsid);
            bool bSame = (activeTargetSsid.isNotEmpty() && b.ssid == activeTargetSsid);
            if (aSame != bSame) return aSame;

            return a.targetDbm > b.targetDbm;
        });

        repaint();
    }

    static int freqToChannel (int freq)
    {
        if (freq == 2484) return 14;
        if (freq >= 2412 && freq <= 2472) return (freq - 2407) / 5;
        if (freq >= 5180 && freq <= 5885) return (freq - 5000) / 5;
        if (freq >= 5955 && freq <= 7115) return (freq - 5950) / 5;
        return 0;
    }

    std::map<juce::String, ApChannelInfo> apMap;
    std::vector<ApChannelInfo> sortedAps;
    juce::String connectedSsid;
    juce::String connectedBssid;
    juce::String activeTargetSsid;
    int sameSsidCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApScannerComponent)
};