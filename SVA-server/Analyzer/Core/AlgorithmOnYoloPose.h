#ifndef ANALYZER_ALGORITHMONYOLOPOSE_H
#define ANALYZER_ALGORITHMONYOLOPOSE_H

#include "Algorithm.h"
#include "PoseOutputLayout.h"
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

namespace SVAAnalyzer
{
    class Config;

    /**
     * @brief ONNX Runtime engine for the fixed-shape YOLO11 COCO Pose export.
     *
     * The detector engine must not be reused for Pose: output0 is
     * [1, 56, 8400], where the final 51 channels are 17 (x,y,confidence)
     * keypoints rather than object classes.
     */
    class PoseOnnxRuntimeEngine
    {
    public:
        PoseOnnxRuntimeEngine(Config *config,
                              const std::string &modelPath,
                              const std::string &algorithmCode);
        ~PoseOnnxRuntimeEngine();

        bool runInference(cv::Mat &image, std::vector<DetectObject> &detects);
        bool isGpuEnabled() const { return mGpuEnabled; }
        const std::string &getActiveProvider() const { return mActiveProvider; }

    private:
        std::string mModelPath;
        std::string mAlgorithmCode;
        std::string mInputNodeName;
        std::string mOutputNodeName;
        int mInputWidth = 0;
        int mInputHeight = 0;
        int mOutputChannels = 0;
        int mPredictionCount = 0;
        PoseOutputLayout mOutputLayout = PoseOutputLayout::ChannelsFirst;
        std::vector<int64_t> mOutputDims;
        bool mGpuEnabled = false;
        std::string mActiveProvider = "CPU";
        Ort::Env mEnv{nullptr};
        Ort::SessionOptions mSessionOptions{nullptr};
        Ort::Session mSession{nullptr};
    };

    class AlgorithmOnYoloPose : public Algorithm
    {
    public:
        AlgorithmOnYoloPose(Config *config,
                            const std::string &modelPath,
                            const std::string &algorithmCode);
        ~AlgorithmOnYoloPose() override;

        bool objectDetect(cv::Mat &image, std::vector<DetectObject> &detects) override;

    private:
        PoseOnnxRuntimeEngine *mEngine = nullptr;
    };
}

#endif // ANALYZER_ALGORITHMONYOLOPOSE_H
