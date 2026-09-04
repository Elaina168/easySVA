#include "SleepPoseEvaluator.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    bool near(float actual, float expected, float tolerance = 0.001f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    SVAAnalyzer::SleepPoseFeatures sleepingFeatures()
    {
        SVAAnalyzer::SleepPoseFeatures features;
        features.valid = true;
        features.validKeypointCount = 7;
        features.headX = 190.0f;
        features.headY = 180.0f;
        features.shoulderCenterX = 150.0f;
        features.shoulderCenterY = 200.0f;
        features.shoulderWidth = 100.0f;
        features.headHeightRatio = 0.20f;
        features.headSideRatio = 0.40f;
        features.shoulderAngleDeg = 0.0f;
        return features;
    }

    SVAAnalyzer::SleepPoseFeatures uprightFeatures()
    {
        SVAAnalyzer::SleepPoseFeatures features = sleepingFeatures();
        features.headX = 150.0f;
        features.headY = 100.0f;
        features.headHeightRatio = 1.00f;
        features.headSideRatio = 0.0f;
        return features;
    }

    SVAAnalyzer::SleepPoseConfig testConfig()
    {
        SVAAnalyzer::SleepPoseConfig config;
        config.confirmWindowMs = 5000;
        config.recoveryMs = 2000;
        config.motionWindowMs = 1000;
        return config;
    }

    void testFeatureExtraction()
    {
        SVAAnalyzer::PoseKeypoints keypoints{};
        keypoints[0] = {190.0f, 180.0f, 0.90f};
        keypoints[5] = {100.0f, 200.0f, 0.90f};
        keypoints[6] = {200.0f, 200.0f, 0.90f};

        const auto features = SVAAnalyzer::SleepPoseProcessor::extractFeatures(
            keypoints,
            testConfig());
        expect(features.valid, "features are valid with one head point and both shoulders");
        expect(features.validKeypointCount == 3, "valid keypoint count is preserved");
        expect(features.headHeightRatio.has_value() && near(*features.headHeightRatio, 0.20f),
               "head height is normalized by shoulder width");
        expect(features.headPitchProxyDeg.has_value() && near(*features.headPitchProxyDeg, 11.3099f),
               "image-plane head pitch proxy is calculated in degrees");
        expect(features.headSideRatio.has_value() && near(*features.headSideRatio, 0.40f),
               "head side offset is normalized by shoulder width");

        keypoints[6].confidence = 0.10f;
        const auto occluded = SVAAnalyzer::SleepPoseProcessor::extractFeatures(
            keypoints,
            testConfig());
        expect(!occluded.valid, "a missing shoulder marks the pose observation invalid");
        expect(occluded.invalidReason == "both shoulders are required",
               "missing-shoulder reason is explainable");
    }

    void testSustainedSleepAlertsOnce()
    {
        SVAAnalyzer::SleepPoseTrackContext context;
        const auto config = testConfig();
        int alertCount = 0;
        SVAAnalyzer::SleepPoseDecision decision;
        for (int64_t timestampMs = 0; timestampMs <= 7000; timestampMs += 1000)
        {
            decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
                context,
                timestampMs,
                sleepingFeatures(),
                config);
            alertCount += decision.alert ? 1 : 0;
        }
        expect(decision.state == SVAAnalyzer::SleepPoseState::Sleep,
               "sustained sleep posture reaches SLEEP");
        expect(alertCount == 1, "SUSPECT to SLEEP raises exactly one alert");
    }

    void testShortHeadDownReturnsNormal()
    {
        SVAAnalyzer::SleepPoseTrackContext context;
        const auto config = testConfig();
        auto decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 0, sleepingFeatures(), config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Suspect,
               "first sleep candidate becomes SUSPECT");
        SVAAnalyzer::SleepPoseProcessor::updateTrack(context, 1000, uprightFeatures(), config);
        decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 3000, uprightFeatures(), config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Normal,
               "short head-down posture recovers without a sleep alert");
        expect(!decision.alert, "short head-down posture does not alert");
    }

    void testInvalidFramesDoNotForceTransition()
    {
        SVAAnalyzer::SleepPoseTrackContext context;
        const auto config = testConfig();
        SVAAnalyzer::SleepPoseProcessor::updateTrack(context, 0, sleepingFeatures(), config);
        SVAAnalyzer::SleepPoseFeatures invalid;
        invalid.invalidReason = "both shoulders are required";
        auto decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 7000, invalid, config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Suspect,
               "invalid pose does not force a state transition");
        expect(!decision.alert, "invalid pose does not alert");
    }

    void testRecoveryKeepsOneEvent()
    {
        SVAAnalyzer::SleepPoseTrackContext context;
        const auto config = testConfig();
        for (int64_t timestampMs = 0; timestampMs <= 5000; timestampMs += 1000)
        {
            SVAAnalyzer::SleepPoseProcessor::updateTrack(
                context, timestampMs, sleepingFeatures(), config);
        }
        auto decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 6000, uprightFeatures(), config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Recover,
               "upright frame after sleep enters RECOVER");
        decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 7000, sleepingFeatures(), config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Sleep,
               "brief interruption returns to the same SLEEP event");
        expect(!decision.alert, "return from RECOVER does not create a duplicate alert");

        SVAAnalyzer::SleepPoseProcessor::updateTrack(context, 8000, uprightFeatures(), config);
        decision = SVAAnalyzer::SleepPoseProcessor::updateTrack(
            context, 10000, uprightFeatures(), config);
        expect(decision.state == SVAAnalyzer::SleepPoseState::Normal,
               "continuous recovery returns to NORMAL");
    }
}

int main()
{
    testFeatureExtraction();
    testSustainedSleepAlertsOnce();
    testShortHeadDownReturnsNormal();
    testInvalidFramesDoNotForceTransition();
    testRecoveryKeepsOneEvent();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d sleep pose test(s) failed\n", failures);
        return 1;
    }
    std::puts("All sleep pose tests passed");
    return 0;
}
