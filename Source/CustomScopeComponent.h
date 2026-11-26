//
// Created by johnny on 2026-11-16. This Header is to use Customized scope with trigger
//
#pragma once

#include "JuceHeader.h"
#include <vector>
#include <atomic>

class CustomScopeComponent : public juce::Component,
                             private juce::Timer
{
public:
    CustomScopeComponent()
    {
        ringBuffer.resize(ringBufferSize, 0.0f);
        startTimerHz(60); // 60fps 부드러운 화면 갱신
    }

    // Wi-Fi RSSI (-100 dBm ~ -20 dBm)를 오실로스코프 표준 범위 [-1.0f, +1.0f]로 정규화해 푸시
    void pushDbmSample(float rssiDbm) noexcept
    {
        // -60 dBm = 중앙(0.0f), -20 dBm = 상단(+1.0f), -100 dBm = 하단(-1.0f)
        const float normalized = juce::jlimit(-1.0f, 1.0f, (rssiDbm + 60.0f) / 40.0f);
        pushBuffer(&normalized, 1);
    }

// 화면 표시용 역변환 (정규화 값 -> dBm)
    static float normalizedToDbm(float norm) noexcept
    {
        return (norm * 40.0f) - 60.0f;
    }


    // 오디오 스레드(processBlock)에서 호출: 락 없이 링 버퍼에 샘플 기록
    void pushBuffer(const float* samples, int numSamples) noexcept
    {
        int pos = writeIndex.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            ringBuffer[pos] = samples[i];
            pos = (pos + 1) & (ringBufferSize - 1);
        }
        writeIndex.store(pos, std::memory_order_release);
    }

    void setSampleRate(double newSampleRate) noexcept { sampleRate = newSampleRate; }
    void setSamplesToShow(int samples) noexcept       { samplesToShow = juce::jlimit(64, 4096, samples); }
    void setVerticalGain(float gain) noexcept         { verticalGain = juce::jlimit(0.2f, 10.0f, gain); }
    void setTriggerLevel(float level) noexcept        { triggerLevel = juce::jlimit(-0.95f, 0.95f, level); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();
        float midY = h * 0.5f;

        // 1. 계측기 배경
        g.fillAll(juce::Colour(0xff0d1117));

        // 2. 10x8 격자 눈금 (Graticule)
        g.setColour(juce::Colour(0xff21262d));
        for (int i = 1; i < 10; ++i)
        {
            float x = w * (i / 10.0f);
            g.drawLine(x, 0.0f, x, h, (i == 5) ? 1.5f : 0.7f);
        }
        for (int j = 1; j < 8; ++j)
        {
            float y = h * (j / 8.0f);
            g.drawLine(0.0f, y, w, y, (j == 4) ? 1.5f : 0.7f);
        }

        // 3. 트리거 레벨 가이드선 (주황색 점선)
        float trigY = midY - (triggerLevel * verticalGain * (h * 0.45f));
        trigY = juce::jlimit(2.0f, h - 2.0f, trigY);
        g.setColour(juce::Colours::orange.withAlpha(0.65f));
        float dashLengths[] = { 5.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(0.0f, trigY, w, trigY), dashLengths, 2, 1.2f);

        // 4. 링 버퍼에서 최신 데이터 스냅샷 & Rising-Edge 트리거 탐색
        int currentWrite = writeIndex.load(std::memory_order_acquire);
        int searchWindow = samplesToShow * 2;
        int startIdx = (currentWrite - searchWindow + ringBufferSize) & (ringBufferSize - 1);

        int triggerOffset = 0;
        bool triggered = false;
        for (int i = 1; i < samplesToShow; ++i)
        {
            int prevIdx = (startIdx + i - 1) & (ringBufferSize - 1);
            int currIdx = (startIdx + i) & (ringBufferSize - 1);
            if (ringBuffer[prevIdx] < triggerLevel && ringBuffer[currIdx] >= triggerLevel)
            {
                triggerOffset = i;
                triggered = true;
                break;
            }
        }

        // 5. 파형 Path 생성 및 Vpp(Peak-to-Peak) 측정
        juce::Path wavePath;
        float minVal = 1.0f, maxVal = -1.0f;

        for (int i = 0; i < samplesToShow; ++i)
        {
            int idx = (startIdx + triggerOffset + i) & (ringBufferSize - 1);
            float sample = ringBuffer[idx];
            minVal = juce::jmin(minVal, sample);
            maxVal = juce::jmax(maxVal, sample);

            float x = (static_cast<float>(i) / static_cast<float>(samplesToShow - 1)) * w;
            float y = midY - (sample * verticalGain * (h * 0.45f));
            y = juce::jlimit(2.0f, h - 2.0f, y);

            if (i == 0) wavePath.startNewSubPath(x, y);
            else        wavePath.lineTo(x, y);
        }

        // 형광 시안/그린 오실로스코프 트레이스
        g.setColour(juce::Colour(0xff00ff9d));
        g.strokePath(wavePath, juce::PathStrokeType(2.0f));

        // 6. 우측 상단 계측 Readout 오버레이 (JUCE 8 FontOptions)
        float vpp = juce::jmax(0.0f, maxVal - minVal);
        float windowMs = (static_cast<float>(samplesToShow) / static_cast<float>(sampleRate)) * 1000.0f;

        g.setFont(juce::FontOptions(13.0f));
        g.setColour(triggered ? juce::Colour(0xff00ff9d) : juce::Colours::orange);
        juce::String statusText = (triggered ? "TRIG: LOCKED  |  " : "TRIG: AUTO    |  ")
                                  + juce::String(windowMs, 1) + " ms/win  |  "
                                  + "Vpp: " + juce::String(vpp, 2);
        g.drawText(statusText, getLocalBounds().reduced(10, 6), juce::Justification::topRight);

        // 테두리 프레임
        g.setColour(juce::Colour(0xff30363d));
        g.drawRect(bounds, 1.5f);
    }

private:
    void timerCallback() override { repaint(); }

    static constexpr int ringBufferSize = 16384; // 2의 거듭제곱 (비트마스크 래핑용)
    std::vector<float> ringBuffer;
    std::atomic<int> writeIndex { 0 };

    double sampleRate = 48000.0;
    int samplesToShow = 512;
    float verticalGain = 1.0f;
    float triggerLevel = 0.0f;
};