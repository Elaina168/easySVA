#include "AlgorithmOnYoloPose.h"
#include "Config.h"
#include "Utils/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <utility>

namespace SVAAnalyzer
{
    namespace
    {
        constexpr int POSE_OUTPUT_CHANNELS = 56;
        constexpr int POSE_KEYPOINT_START_CHANNEL = 5;
        constexpr float DETECTION_CONFIDENCE = 0.35f;
        constexpr float NMS_IOU_THRESHOLD = 0.45f;

        struct LetterboxTransform
        {
            float scale = 1.0f;
            int padLeft = 0;
            int padTop = 0;
        };

        struct PoseCandidate
        {
            cv::Rect box;
            float confidence = 0.0f;
            PoseKeypoints keypoints{};
        };

        cv::Mat ensureBgr(const cv::Mat &image)
        {
            if (image.channels() == 3)
            {
                return image;
            }

            cv::Mat bgr;
            if (image.channels() == 1)
            {
                cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
                return bgr;
            }
            if (image.channels() == 4)
            {
                cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
                return bgr;
            }
            return {};
        }

        cv::Mat centeredLetterbox(const cv::Mat &source,
                                  int targetWidth,
                                  int targetHeight,
                                  LetterboxTransform &transform)
        {
            transform.scale = std::min(
                static_cast<float>(targetWidth) / static_cast<float>(source.cols),
                static_cast<float>(targetHeight) / static_cast<float>(source.rows));

            const int resizedWidth = std::min(
                targetWidth,
                std::max(1, static_cast<int>(std::round(source.cols * transform.scale))));
            const int resizedHeight = std::min(
                targetHeight,
                std::max(1, static_cast<int>(std::round(source.rows * transform.scale))));

            cv::Mat resized;
            cv::resize(source, resized, cv::Size(resizedWidth, resizedHeight), 0.0, 0.0, cv::INTER_LINEAR);

            const int horizontalPadding = targetWidth - resizedWidth;
            const int verticalPadding = targetHeight - resizedHeight;
            transform.padLeft = horizontalPadding / 2;
            transform.padTop = verticalPadding / 2;
            const int padRight = horizontalPadding - transform.padLeft;
            const int padBottom = verticalPadding - transform.padTop;

            cv::Mat result;
            cv::copyMakeBorder(resized,
                               result,
                               transform.padTop,
                               padBottom,
                               transform.padLeft,
                               padRight,
                               cv::BORDER_CONSTANT,
                               cv::Scalar(114, 114, 114));
            return result;
        }

        float clampBoxCoordinate(float value, int size)
        {
            return std::max(0.0f, std::min(value, static_cast<float>(std::max(0, size))));
        }

        float clampPointCoordinate(float value, int size)
        {
            return std::max(0.0f, std::min(value, static_cast<float>(std::max(0, size - 1))));
        }
    }

    PoseOnnxRuntimeEngine::PoseOnnxRuntimeEngine(Config *,
                                                 const std::string &modelPath,
                                                 const std::string &algorithmCode)
        : mModelPath(modelPath), mAlgorithmCode(algorithmCode)
    {
        LOGI("Pose modelPath=%s", modelPath.c_str());

        mEnv = Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "YOLO_POSE");
        mSessionOptions = Ort::SessionOptions();
        mSessionOptions.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

        const std::vector<std::string> providers = Ort::GetAvailableProviders();
        bool gpuAssigned = false;

#if SVA_ONNXRUNTIME_GPU
        const auto trtProvider = std::find(providers.begin(), providers.end(), "TensorrtExecutionProvider");
        if (trtProvider != providers.end())
        {
            try
            {
                OrtTensorRTProviderOptions trtOptions{};
                trtOptions.device_id = 0;
                trtOptions.trt_max_workspace_size = 1ULL << 30;
                trtOptions.trt_fp16_enable = 1;
                trtOptions.trt_max_partition_iterations = 1000;
                trtOptions.trt_min_subgraph_size = 1;
                trtOptions.trt_engine_cache_path = "/opt/SVA/tmp/trt_cache";
                trtOptions.trt_engine_cache_enable = 1;
                mSessionOptions.AppendExecutionProvider_TensorRT(trtOptions);
                gpuAssigned = true;
                mGpuEnabled = true;
                mActiveProvider = "TensorRT";
            }
            catch (const Ort::Exception &e)
            {
                LOGI("Pose TensorRT provider unavailable: %s", e.what());
            }
        }

        const auto cudaProvider = std::find(providers.begin(), providers.end(), "CUDAExecutionProvider");
        if (cudaProvider != providers.end())
        {
            try
            {
                OrtCUDAProviderOptions cudaOptions{};
                cudaOptions.device_id = 0;
                mSessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
                gpuAssigned = true;
                mGpuEnabled = true;
                mActiveProvider = mActiveProvider == "TensorRT" ? "TensorRT/CUDA" : "CUDA";
            }
            catch (const Ort::Exception &e)
            {
                LOGI("Pose CUDA provider unavailable: %s", e.what());
            }
        }
#else
        LOGI("Pose SVA_ONNXRUNTIME_GPU=OFF, use CPUExecutionProvider only");
#endif

        auto createSession = [this, &modelPath]() {
#ifdef WIN32
            const std::wstring wideModelPath(modelPath.begin(), modelPath.end());
            return Ort::Session(mEnv, wideModelPath.c_str(), mSessionOptions);
#else
            return Ort::Session(mEnv, modelPath.c_str(), mSessionOptions);
#endif
        };

        try
        {
            mSession = createSession();
        }
        catch (const Ort::Exception &e)
        {
            if (!gpuAssigned)
            {
                throw;
            }
            LOGI("Pose GPU session failed: %s, retry with CPUExecutionProvider", e.what());
            mSessionOptions = Ort::SessionOptions();
            mSessionOptions.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
            mGpuEnabled = false;
            mActiveProvider = "CPU";
            mSession = createSession();
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto inputName = mSession.GetInputNameAllocated(0, allocator);
        mInputNodeName = inputName.get();
        const auto inputDims = mSession.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inputDims.size() != 4 || inputDims[0] != 1 || inputDims[1] != 3 ||
            inputDims[2] <= 0 || inputDims[3] <= 0)
        {
            throw std::runtime_error("Pose model input must be fixed NCHW [1,3,H,W]");
        }
        mInputHeight = static_cast<int>(inputDims[2]);
        mInputWidth = static_cast<int>(inputDims[3]);

        const auto outputName = mSession.GetOutputNameAllocated(0, allocator);
        mOutputNodeName = outputName.get();
        mOutputDims = mSession.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const PoseOutputShape outputShape = inspectPoseOutputShape(mOutputDims);
        if (outputShape.channels != POSE_OUTPUT_CHANNELS)
        {
            throw std::runtime_error("Pose model output must contain 56 channels");
        }
        mOutputLayout = outputShape.layout;
        mOutputChannels = outputShape.channels;
        mPredictionCount = outputShape.predictionCount;

        const char *layoutName = mOutputLayout == PoseOutputLayout::ChannelsFirst
                                     ? "[1,56,N]"
                                     : "[1,N,56]";
        LOGI("Pose ONNX input=%s [1,3,%d,%d] output=%s [%lld,%lld,%lld] layout=%s provider=%s",
             mInputNodeName.c_str(),
             mInputHeight,
             mInputWidth,
             mOutputNodeName.c_str(),
             static_cast<long long>(mOutputDims[0]),
             static_cast<long long>(mOutputDims[1]),
             static_cast<long long>(mOutputDims[2]),
             layoutName,
             mActiveProvider.c_str());
    }

    PoseOnnxRuntimeEngine::~PoseOnnxRuntimeEngine() = default;

    bool PoseOnnxRuntimeEngine::runInference(cv::Mat &image, std::vector<DetectObject> &detects)
    {
        detects.clear();
        if (image.empty() || mInputWidth <= 0 || mInputHeight <= 0 || mPredictionCount <= 0)
        {
            LOGE("Pose invalid inference input");
            return false;
        }

        const cv::Mat bgr = ensureBgr(image);
        if (bgr.empty())
        {
            LOGE("Pose unsupported image channels=%d", image.channels());
            return false;
        }

        LetterboxTransform transform;
        const cv::Mat inputImage = centeredLetterbox(bgr, mInputWidth, mInputHeight, transform);
        cv::Mat blob = cv::dnn::blobFromImage(inputImage,
                                              1.0 / 255.0,
                                              cv::Size(mInputWidth, mInputHeight),
                                              cv::Scalar(),
                                              true,
                                              false,
                                              CV_32F);

        const std::array<int64_t, 4> inputShape{1, 3, mInputHeight, mInputWidth};
        const size_t inputElementCount = static_cast<size_t>(3) *
                                         static_cast<size_t>(mInputHeight) *
                                         static_cast<size_t>(mInputWidth);
        const Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo,
                                                                 blob.ptr<float>(),
                                                                 inputElementCount,
                                                                 inputShape.data(),
                                                                 inputShape.size());
        const std::array<const char *, 1> inputNames{mInputNodeName.c_str()};
        const std::array<const char *, 1> outputNames{mOutputNodeName.c_str()};
        std::vector<Ort::Value> outputs = mSession.Run(Ort::RunOptions{nullptr},
                                                       inputNames.data(),
                                                       &inputTensor,
                                                       1,
                                                       outputNames.data(),
                                                       outputNames.size());
        if (outputs.empty() || !outputs[0].IsTensor())
        {
            LOGE("Pose inference returned no tensor output");
            return false;
        }

        const auto actualOutputDims = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        PoseOutputShape actualOutputShape;
        try
        {
            actualOutputShape = inspectPoseOutputShape(actualOutputDims);
        }
        catch (const std::invalid_argument &e)
        {
            LOGE("Pose runtime output shape is invalid: %s", e.what());
            return false;
        }
        if (actualOutputShape.layout != mOutputLayout ||
            actualOutputShape.channels != mOutputChannels ||
            actualOutputShape.predictionCount != mPredictionCount)
        {
            LOGE("Pose runtime output shape does not match the inspected model contract");
            return false;
        }

        const float *output = outputs[0].GetTensorData<float>();
        if (!output)
        {
            LOGE("Pose inference returned null tensor data");
            return false;
        }

        auto valueAt = [output, &actualOutputShape](int channel, int prediction) -> float {
            return poseOutputValue(output, actualOutputShape, channel, prediction);
        };
        auto mapBoxX = [&transform, &image](float x) -> float {
            return clampBoxCoordinate((x - static_cast<float>(transform.padLeft)) / transform.scale, image.cols);
        };
        auto mapBoxY = [&transform, &image](float y) -> float {
            return clampBoxCoordinate((y - static_cast<float>(transform.padTop)) / transform.scale, image.rows);
        };
        auto mapPointX = [&transform, &image](float x) -> float {
            return clampPointCoordinate((x - static_cast<float>(transform.padLeft)) / transform.scale, image.cols);
        };
        auto mapPointY = [&transform, &image](float y) -> float {
            return clampPointCoordinate((y - static_cast<float>(transform.padTop)) / transform.scale, image.rows);
        };

        std::vector<PoseCandidate> candidates;
        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        candidates.reserve(64);
        boxes.reserve(64);
        confidences.reserve(64);

        for (int prediction = 0; prediction < mPredictionCount; ++prediction)
        {
            const float confidence = valueAt(4, prediction);
            if (!std::isfinite(confidence) || confidence < DETECTION_CONFIDENCE)
            {
                continue;
            }

            const float centerX = valueAt(0, prediction);
            const float centerY = valueAt(1, prediction);
            const float width = valueAt(2, prediction);
            const float height = valueAt(3, prediction);
            if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
                !std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f)
            {
                continue;
            }

            const float mappedLeft = mapBoxX(centerX - width * 0.5f);
            const float mappedTop = mapBoxY(centerY - height * 0.5f);
            const float mappedRight = mapBoxX(centerX + width * 0.5f);
            const float mappedBottom = mapBoxY(centerY + height * 0.5f);
            const int left = static_cast<int>(std::floor(mappedLeft));
            const int top = static_cast<int>(std::floor(mappedTop));
            const int right = static_cast<int>(std::ceil(mappedRight));
            const int bottom = static_cast<int>(std::ceil(mappedBottom));
            if (right <= left || bottom <= top)
            {
                continue;
            }

            PoseCandidate candidate;
            candidate.box = cv::Rect(left, top, right - left, bottom - top);
            candidate.confidence = confidence;
            for (std::size_t keypointIndex = 0; keypointIndex < COCO_POSE_KEYPOINT_COUNT; ++keypointIndex)
            {
                const int channel = POSE_KEYPOINT_START_CHANNEL + static_cast<int>(keypointIndex) * 3;
                const float x = valueAt(channel, prediction);
                const float y = valueAt(channel + 1, prediction);
                const float keypointConfidence = valueAt(channel + 2, prediction);
                PoseKeypoint &keypoint = candidate.keypoints[keypointIndex];
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(keypointConfidence))
                {
                    keypoint.x = mapPointX(x);
                    keypoint.y = mapPointY(y);
                    keypoint.confidence = std::max(0.0f, std::min(keypointConfidence, 1.0f));
                }
            }

            boxes.push_back(candidate.box);
            confidences.push_back(candidate.confidence);
            candidates.push_back(candidate);
        }

        std::vector<int> keptIndices;
        cv::dnn::NMSBoxes(boxes,
                          confidences,
                          DETECTION_CONFIDENCE,
                          NMS_IOU_THRESHOLD,
                          keptIndices);
        detects.reserve(keptIndices.size());
        for (const int keptIndex : keptIndices)
        {
            if (keptIndex < 0 || keptIndex >= static_cast<int>(candidates.size()))
            {
                continue;
            }
            const PoseCandidate &candidate = candidates[static_cast<size_t>(keptIndex)];
            DetectObject detect;
            detect.x1 = candidate.box.x;
            detect.y1 = candidate.box.y;
            detect.x2 = candidate.box.x + candidate.box.width;
            detect.y2 = candidate.box.y + candidate.box.height;
            detect.class_score = candidate.confidence;
            detect.class_id = 0;
            detect.class_name = "person";
            detect.source_algorithm = mAlgorithmCode;
            detect.hasPose = true;
            detect.keypoints = candidate.keypoints;
            detects.push_back(detect);
        }
        return true;
    }

    AlgorithmOnYoloPose::AlgorithmOnYoloPose(Config *config,
                                             const std::string &modelPath,
                                             const std::string &algorithmCode)
        : Algorithm(config)
    {
        mEngine = new PoseOnnxRuntimeEngine(config, modelPath, algorithmCode);
    }

    AlgorithmOnYoloPose::~AlgorithmOnYoloPose()
    {
        delete mEngine;
        mEngine = nullptr;
    }

    bool AlgorithmOnYoloPose::objectDetect(cv::Mat &image, std::vector<DetectObject> &detects)
    {
        return mEngine && mEngine->runInference(image, detects);
    }
}
