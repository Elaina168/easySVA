#ifndef ANALYZER_POSEOUTPUTLAYOUT_H
#define ANALYZER_POSEOUTPUTLAYOUT_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace SVAAnalyzer
{
    enum class PoseOutputLayout
    {
        ChannelsFirst,
        PredictionsFirst
    };

    struct PoseOutputShape
    {
        PoseOutputLayout layout;
        int channels;
        int predictionCount;
    };

    inline PoseOutputShape inspectPoseOutputShape(const std::vector<std::int64_t> &dims)
    {
        if (dims.size() != 3 || dims[0] != 1)
        {
            throw std::invalid_argument("Pose output must have shape [1,56,N] or [1,N,56]");
        }
        if (dims[1] == 56 && dims[2] > 0)
        {
            return {PoseOutputLayout::ChannelsFirst, 56, static_cast<int>(dims[2])};
        }
        if (dims[2] == 56 && dims[1] > 0)
        {
            return {PoseOutputLayout::PredictionsFirst, 56, static_cast<int>(dims[1])};
        }
        throw std::invalid_argument("Pose output must have shape [1,56,N] or [1,N,56]");
    }

    inline float poseOutputValue(const float *data,
                                 const PoseOutputShape &shape,
                                 int channel,
                                 int prediction)
    {
        if (!data || channel < 0 || channel >= shape.channels ||
            prediction < 0 || prediction >= shape.predictionCount)
        {
            throw std::out_of_range("Pose output channel or prediction is out of range");
        }
        const std::size_t index = shape.layout == PoseOutputLayout::ChannelsFirst
            ? static_cast<std::size_t>(channel) * static_cast<std::size_t>(shape.predictionCount) +
                  static_cast<std::size_t>(prediction)
            : static_cast<std::size_t>(prediction) * static_cast<std::size_t>(shape.channels) +
                  static_cast<std::size_t>(channel);
        return data[index];
    }
}

#endif // ANALYZER_POSEOUTPUTLAYOUT_H
