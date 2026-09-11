#include "PoseOutputLayout.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

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

    void expectThrows(const std::vector<int64_t> &dims, const char *message)
    {
        try
        {
            SVAAnalyzer::inspectPoseOutputShape(dims);
            expect(false, message);
        }
        catch (const std::invalid_argument &)
        {
            expect(true, message);
        }
    }
}

int main()
{
    const int predictionCount = 2;
    std::vector<float> channelsFirst(56 * predictionCount);
    std::vector<float> predictionsFirst(56 * predictionCount);
    for (int channel = 0; channel < 56; ++channel)
    {
        for (int prediction = 0; prediction < predictionCount; ++prediction)
        {
            channelsFirst[channel * predictionCount + prediction] =
                static_cast<float>(channel * 100 + prediction);
            predictionsFirst[prediction * 56 + channel] =
                static_cast<float>(channel * 100 + prediction);
        }
    }

    const auto channelsFirstShape = SVAAnalyzer::inspectPoseOutputShape({1, 56, predictionCount});
    const auto predictionsFirstShape = SVAAnalyzer::inspectPoseOutputShape({1, predictionCount, 56});
    for (int channel : {0, 4, 55})
    {
        for (int prediction = 0; prediction < predictionCount; ++prediction)
        {
            const float expected = static_cast<float>(channel * 100 + prediction);
            expect(std::abs(SVAAnalyzer::poseOutputValue(channelsFirst.data(), channelsFirstShape, channel, prediction) - expected) < 0.001f,
                   "channels-first value keeps channel/prediction semantics");
            expect(std::abs(SVAAnalyzer::poseOutputValue(predictionsFirst.data(), predictionsFirstShape, channel, prediction) - expected) < 0.001f,
                   "predictions-first value keeps channel/prediction semantics");
        }
    }

    expectThrows({1, 55, 2}, "55-channel output is rejected");
    expectThrows({1, 2, 55}, "transposed 55-channel output is rejected");
    expectThrows({2, 56, 2}, "batch size other than one is rejected");
    expectThrows({1, 56, 2, 1}, "non-three-dimensional output is rejected");
    return failures == 0 ? 0 : 1;
}
