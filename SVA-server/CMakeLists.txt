cmake_minimum_required(VERSION 3.16.0)
project(Analyzer)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread ")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

set(ARCH_X86_64 "x86_64")
set(ARCH_AARCH64 "aarch64")
message(STATUS "System architecture: ${CMAKE_SYSTEM_PROCESSOR}")

set(ONNXRUNTIME_ROOT "/usr/local/onnxruntime" CACHE PATH "ONNX Runtime installation root")
option(SVA_ONNXRUNTIME_GPU "Enable ONNX Runtime TensorRT/CUDA execution providers" ON)
message(STATUS "ONNX Runtime root: ${ONNXRUNTIME_ROOT}")
message(STATUS "SVA_ONNXRUNTIME_GPU: ${SVA_ONNXRUNTIME_GPU}")

set(SVA_DEP_ROOT "/usr/local" CACHE PATH "Third-party dependency prefix")

# third lib start
include_directories(/usr/local/include)
include_directories(${SVA_DEP_ROOT}/include)
link_directories(/usr/local/lib)
link_directories(/usr/local/lib64)
link_directories(${SVA_DEP_ROOT}/lib)
link_directories(${SVA_DEP_ROOT}/lib64)
include_directories(/usr/include/jsoncpp) #jsoncpp
include_directories(${SVA_DEP_ROOT}/include/jsoncpp)
include_directories(${SVA_DEP_ROOT}/include/x86_64-linux-gnu)
include_directories(/usr/local/ffmpeg/include) #ffmpeg
include_directories(${SVA_DEP_ROOT}/include/ffmpeg)
link_directories(/usr/local/ffmpeg/lib) #ffmpeg
link_directories(${SVA_DEP_ROOT}/lib/x86_64-linux-gnu)
find_package(OpenCV REQUIRED) #opencv

include_directories(${ONNXRUNTIME_ROOT}/include) #onnxruntime
link_directories(${ONNXRUNTIME_ROOT}/lib) #onnxruntime	

# third lib end

set(source
        Analyzer/Core/Utils/CalcuIOU.cpp
        Analyzer/Core/Utils/Request.cpp
        Analyzer/Core/Utils/GeometryUtils.cpp
        Analyzer/Core/Algorithm.cpp
        Analyzer/Core/AlgorithmOnYolo.cpp
        Analyzer/Core/AlgorithmOnYoloPose.cpp
        Analyzer/Core/Analyzer.cpp
        Analyzer/Core/AvPullStream.cpp
        Analyzer/Core/AvPushStream.cpp
        Analyzer/Core/BehaviorEvaluator.cpp
        Analyzer/Core/Config.cpp
        Analyzer/Core/Frame.cpp
        Analyzer/Core/GenerateAlarmVideo.cpp
        Analyzer/Core/Scheduler.cpp
        Analyzer/Core/Server.cpp
        Analyzer/Core/SleepPoseEvaluator.cpp
        Analyzer/Core/TemporalContext.cpp
        Analyzer/Core/Worker.cpp
        Analyzer/main.cpp)

add_executable(Analyzer ${source})
if(SVA_ONNXRUNTIME_GPU)
  target_compile_definitions(Analyzer PRIVATE SVA_ONNXRUNTIME_GPU=1)
else()
  target_compile_definitions(Analyzer PRIVATE SVA_ONNXRUNTIME_GPU=0)
endif()
target_link_libraries(Analyzer ${OpenCV_LIBS})
target_link_libraries(Analyzer event curl crypto jsoncpp avformat avcodec avutil swscale swresample pthread)
target_link_libraries(Analyzer onnxruntime) #onnxruntime
target_link_options(Analyzer PRIVATE "-Wl,--allow-shlib-undefined")

option(SVA_BUILD_POSE_SMOKE_TEST "Build the standalone YOLO Pose video smoke test" ON)
if(SVA_BUILD_POSE_SMOKE_TEST)
  add_executable(PoseSmokeTest
          Analyzer/Tools/PoseSmokeTest.cpp
          Analyzer/Core/Algorithm.cpp
          Analyzer/Core/AlgorithmOnYoloPose.cpp
          Analyzer/Core/SleepPoseEvaluator.cpp)
  target_include_directories(PoseSmokeTest PRIVATE Analyzer/Core)
  if(SVA_ONNXRUNTIME_GPU)
    target_compile_definitions(PoseSmokeTest PRIVATE SVA_ONNXRUNTIME_GPU=1)
  else()
    target_compile_definitions(PoseSmokeTest PRIVATE SVA_ONNXRUNTIME_GPU=0)
  endif()
  target_link_libraries(PoseSmokeTest ${OpenCV_LIBS} onnxruntime pthread)
  target_link_options(PoseSmokeTest PRIVATE "-Wl,--allow-shlib-undefined")
endif()

if(BUILD_TESTING)
  add_executable(SleepPoseUnitTest
          Analyzer/Tools/SleepPoseUnitTest.cpp
          Analyzer/Core/SleepPoseEvaluator.cpp)
  target_include_directories(SleepPoseUnitTest PRIVATE Analyzer/Core)
  target_link_libraries(SleepPoseUnitTest ${OpenCV_LIBS})
  target_link_options(SleepPoseUnitTest PRIVATE "-Wl,--allow-shlib-undefined")
  add_test(NAME SleepPoseUnitTest COMMAND SleepPoseUnitTest)

  add_executable(StreamUrlUnitTest
          Analyzer/Tools/StreamUrlUnitTest.cpp
          Analyzer/Core/AvPullStream.cpp
          Analyzer/Core/Config.cpp)
  target_include_directories(StreamUrlUnitTest PRIVATE Analyzer/Core ${SVA_DEP_ROOT}/include /usr/include/jsoncpp)
  target_link_libraries(StreamUrlUnitTest ${OpenCV_LIBS} avformat avcodec avutil jsoncpp pthread)
  target_link_options(StreamUrlUnitTest PRIVATE "-Wl,--allow-shlib-undefined")
  add_test(NAME StreamUrlUnitTest COMMAND StreamUrlUnitTest)
endif()
