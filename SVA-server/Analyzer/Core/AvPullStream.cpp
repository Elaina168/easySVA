#include "AvPullStream.h"
#include "Config.h"
#include "Utils/Log.h"
#include "Utils/Common.h"
#include "Scheduler.h"
#include "Control.h"
#include "Worker.h"
namespace SVAAnalyzer
{
    AvPullStream::AvPullStream(Worker *worker) : mWorker(worker),
                                                 mHwDeviceCtx(nullptr)
    {
        LOGI("");
    }

    AvPullStream::~AvPullStream()
    {
        LOGI("");

        // 释放硬件设备上下文
        if (mHwDeviceCtx)
        {
            av_buffer_unref(&mHwDeviceCtx);
            mHwDeviceCtx = nullptr;
        }

        closeConnect();
    }

    std::string AvPullStream::normalizeStreamUrl(const std::string &sourceUrl, int defaultRtspPort)
    {
        const std::string gbPrefix = "gb28181://";
        const std::string shortGbPrefix = "gb://";
        std::string value = sourceUrl;
        std::string prefix;
        if (value.rfind(gbPrefix, 0) == 0) prefix = gbPrefix;
        else if (value.rfind(shortGbPrefix, 0) == 0) prefix = shortGbPrefix;
        if (prefix.empty()) return value;

        std::string authorityAndPath = value.substr(prefix.size());
        const size_t slash = authorityAndPath.find('/');
        std::string authority = slash == std::string::npos ? authorityAndPath : authorityAndPath.substr(0, slash);
        std::string path = slash == std::string::npos ? "" : authorityAndPath.substr(slash);
        if (authority.empty()) return value;
        // GB playback URLs are normally `host:port/app/stream`. When a port
        // is omitted, use the configured ZLMediaKit RTSP port.
        if (authority.find(':') == std::string::npos && defaultRtspPort > 0)
        {
            authority += ":" + std::to_string(defaultRtspPort);
        }
        if (path.empty() || path == "/")
        {
            LOGE("invalid GB28181 playback URL (missing app/stream): %s", sourceUrl.c_str());
            return value;
        }
        return "rtsp://" + authority + path;
    }

    bool AvPullStream::connect()
    {
        const int defaultRtspPort = (mWorker && mWorker->mScheduler && mWorker->mScheduler->getConfig())
            ? mWorker->mScheduler->getConfig()->mediaRtspPort : 9994;
        const std::string sourceUrl = (mWorker && mWorker->mControl) ? mWorker->mControl->streamUrl : "";
        std::string streamUrl = normalizeStreamUrl(sourceUrl, defaultRtspPort);
        if (streamUrl != sourceUrl)
        {
            LOGI("GB28181 source normalized for unified decoder: %s -> %s", sourceUrl.c_str(), streamUrl.c_str());
        }

        const bool isGbStream = (mWorker && mWorker->mControl && mWorker->mControl->streamProtocol == "gb28181") ||
                                (sourceUrl.rfind("gb28181://", 0) == 0 || sourceUrl.rfind("gb://", 0) == 0);

        // For GB28181 streams, ZLMediaKit may take 1-3 seconds to begin receiving RTP stream
        // after SIP INVITE, so retry initial probe up to 5 times.
        const int maxAttempts = isGbStream ? 5 : 1;
        int ret = -1;

        for (int attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            mFmtCtx = avformat_alloc_context();

            AVDictionary *fmt_options = NULL;
            av_dict_set(&fmt_options, "rtsp_transport", "tcp", 0); // 设置rtsp底层网络协议 tcp or udp
            av_dict_set(&fmt_options, "stimeout", "10000000", 0);  // 设置rtsp连接超时（单位 us）1秒=1000000
            av_dict_set(&fmt_options, "rw_timeout", "1000000", 0); // 设置rtmp/http-flv连接超时（单位 us）

            ret = avformat_open_input(&mFmtCtx, streamUrl.data(), NULL, &fmt_options);
            if (fmt_options)
            {
                av_dict_free(&fmt_options);
            }

            if (ret == 0)
            {
                if (attempt > 1)
                {
                    LOGI("avformat_open_input success on attempt %d for GB28181 stream: %s", attempt, streamUrl.c_str());
                }
                break;
            }

            if (mFmtCtx)
            {
                avformat_close_input(&mFmtCtx);
                mFmtCtx = NULL;
            }

            if (attempt < maxAttempts)
            {
                LOGI("GB28181 stream waiting for RTP ingestion (attempt %d/%d), retry in 1s: %s",
                     attempt, maxAttempts, streamUrl.c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        if (ret != 0)
        {
            LOGE("avformat_open_input error: url=%s (ret=%d)", streamUrl.data(), ret);
            return false;
        }

        if (avformat_find_stream_info(mFmtCtx, NULL) < 0)
        {
            LOGE("avformat_find_stream_info error");
            return false;
        }

        // video start
        mWorker->mControl->videoIndex = av_find_best_stream(mFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        if (mWorker->mControl->videoIndex > -1)
        {
            AVCodecParameters *videoCodecPar = mFmtCtx->streams[mWorker->mControl->videoIndex]->codecpar;

            const AVCodec *videoCodec = NULL;
            if (!videoCodec)
            {
                videoCodec = avcodec_find_decoder(videoCodecPar->codec_id);
                if (!videoCodec)
                {
                    LOGE("avcodec_find_decoder error");
                    return false;
                }
            }

            mVideoCodecCtx = avcodec_alloc_context3(videoCodec);
            if (avcodec_parameters_to_context(mVideoCodecCtx, videoCodecPar) != 0)
            {
                LOGE("avcodec_parameters_to_context error");
                return false;
            }

            // ========== 添加硬件解码支持 ==========
            // 1. 创建CUDA硬件设备上下文
            enum AVHWDeviceType hw_type = av_hwdevice_find_type_by_name("cuda");
            if (hw_type != AV_HWDEVICE_TYPE_NONE)
            {
                int ret = av_hwdevice_ctx_create(&mHwDeviceCtx, hw_type, NULL, NULL, 0);
                if (ret == 0)
                {
                    // 2. 将硬件设备上下文设置到解码器
                    mVideoCodecCtx->hw_device_ctx = av_buffer_ref(mHwDeviceCtx);
                    LOGI("CUDA hardware decoding ENABLED for stream: %s", streamUrl.c_str());
                }
                else
                {
                    LOGI("Failed to create CUDA device context, using CPU decoding");
                }
            }
            else
            {
                LOGI("CUDA hardware decoding not found, using CPU decoding");
            }
            // ========== 结束硬件解码支持 ==========

            if (avcodec_open2(mVideoCodecCtx, videoCodec, nullptr) < 0)
            {
                LOGE("avcodec_open2 error");
                return false;
            }
            // mVideoCodecCtx->thread_count = 1;

            mVideoStream = mFmtCtx->streams[mWorker->mControl->videoIndex];
            if (0 == mVideoStream->avg_frame_rate.den || mVideoStream->avg_frame_rate.num <= 0)
            {

                LOGI("videoIndex=%d, invalid avg_frame_rate %d/%d, fallback to 25fps",
                     mWorker->mControl->videoIndex, mVideoStream->avg_frame_rate.num, mVideoStream->avg_frame_rate.den);

                mWorker->mControl->videoFps = 25;
            }
            else
            {
                mWorker->mControl->videoFps = mVideoStream->avg_frame_rate.num / mVideoStream->avg_frame_rate.den;
                if (mWorker->mControl->videoFps <= 0)
                {
                    mWorker->mControl->videoFps = 25;
                }
            }

            mWorker->mControl->videoWidth = mVideoCodecCtx->width;
            mWorker->mControl->videoHeight = mVideoCodecCtx->height;
            mWorker->mControl->videoChannel = 3;

            if (!mWorker->mControl->parseRecognitionRegion())
            {
                LOGE("parseRecognitionRegion() error");
                return false;
            }
        }
        else
        {
            LOGE("av_find_best_stream video error videoIndex=%d", mWorker->mControl->videoIndex);
            return false;
        }
        // video end;

        // audio start

        // audio end

        if (mWorker->mControl->videoIndex <= -1)
        {
            return false;
        }

        mConnectCount++;

        return true;
    }

    bool AvPullStream::reConnect()
    {

        if (mConnectCount <= 100)
        {
            closeConnect();

            if (connect())
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        return false;
    }
    void AvPullStream::closeConnect()
    {

        LOGI("");

        clearVideoPktQueue();
    mVideoPktQ_cv.notify_all();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // 释放硬件设备上下文
        if (mHwDeviceCtx)
        {
            av_buffer_unref(&mHwDeviceCtx);
            mHwDeviceCtx = nullptr;
        }

        if (mVideoCodecCtx)
        {

            avcodec_close(mVideoCodecCtx);
            avcodec_free_context(&mVideoCodecCtx);
            mVideoCodecCtx = NULL;
            if (mWorker && mWorker->mControl)
            {
                mWorker->mControl->videoIndex = -1;
            }
        }

        if (mFmtCtx)
        {
            // 拉流不需要释放start
            // if (mFmtCtx && !(mFmtCtx->oformat->flags & AVFMT_NOFILE)) {
            //    avio_close(mFmtCtx->pb);
            //}
            // 拉流不需要释放end
            avformat_close_input(&mFmtCtx);
            mFmtCtx = NULL;
        }
    }

    bool AvPullStream::pushVideoPkt(const AVPacket &pkt)
    {
        AVPacket copied_pkt;
        av_init_packet(&copied_pkt);
        if (av_packet_ref(&copied_pkt, &pkt) < 0)
        {
            return false;
        }

        std::unique_lock<std::mutex> lock(mVideoPktQ_mtx);
        if (mVideoPktQ.size() >= mVideoPktQueueCapacity)
        {
            AVPacket dropped = mVideoPktQ.front();
            mVideoPktQ.pop();
            av_packet_unref(&dropped);
        }
        mVideoPktQ.push(copied_pkt);
        lock.unlock();
        mVideoPktQ_cv.notify_one();

        return true;
    }
    bool AvPullStream::getVideoPkt(AVPacket &pkt, int &pktQSize)
    {

        std::unique_lock<std::mutex> lock(mVideoPktQ_mtx);
        mVideoPktQ_cv.wait_for(lock, std::chrono::milliseconds(20), [this]() {
            return !mVideoPktQ.empty() || !mWorker->getState();
        });

        if (!mVideoPktQ.empty())
        {
            av_packet_move_ref(&pkt, &mVideoPktQ.front());
            mVideoPktQ.pop();
            pktQSize = static_cast<int>(mVideoPktQ.size());
            return true;
        }
        return false;
    }
    int AvPullStream::getVideoPktQueueSize()
    {
        std::lock_guard<std::mutex> lock(mVideoPktQ_mtx);
        return static_cast<int>(mVideoPktQ.size());
    }
    void AvPullStream::clearVideoPktQueue()
    {
        mVideoPktQ_mtx.lock();
        while (!mVideoPktQ.empty())
        {
            AVPacket pkt = mVideoPktQ.front();
            mVideoPktQ.pop();

            av_packet_unref(&pkt);
        }
        mVideoPktQ_mtx.unlock();
    }
    void AvPullStream::handleRead()
    {
        int continuity_error_count = 0;

        AVPacket pkt = {};
        while (mWorker->getState())
        {
            if (av_read_frame(mFmtCtx, &pkt) >= 0)
            {
                continuity_error_count = 0;

                if (pkt.stream_index == mWorker->mControl->videoIndex)
                {
                    //                    mVideoPktQ_mtx.lock();
                    //                    int pktQSize = mVideoPktQ.size();
                    //                    mVideoPktQ_mtx.unlock();
                    //                    if(pktQSize > 0){
                    //                        this->clearVideoPktQueue();
                    //                    }

                    pushVideoPkt(pkt);
                    av_packet_unref(&pkt);
                }
                else
                {
                    // av_free_packet(&pkt);//过时
                    av_packet_unref(&pkt);
                }
            }
            else
            {
                continuity_error_count++;
                if (continuity_error_count > 5)
                { // 大于5秒重启拉流连接

                    LOGE("av_read_frame error, continuity_error_count = %d (s)", continuity_error_count);

                    if (reConnect())
                    {
                        continuity_error_count = 0;
                        LOGI("reConnect success : mConnectCount=%d", mConnectCount);
                    }
                    else
                    {
                        LOGI("reConnect error : mConnectCount=%d", mConnectCount);
                        mWorker->remove();
                    }
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
        }
    }
    void AvPullStream::readThread(void *arg)
    {

        AvPullStream *pullStream = (AvPullStream *)arg;
        pullStream->handleRead();
    }
}
