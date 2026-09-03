set(GB28181_CORE_SOURCES
  core/DigestAuth.cpp
  core/DigestAuth.h
  core/SipMessage.cpp
  core/SipMessage.h
  core/SipStreamDecoder.cpp
  core/SipStreamDecoder.h
  core/SipResponse.cpp
  core/SipResponse.h)

add_library(easy_sva_gb28181_core STATIC ${GB28181_CORE_SOURCES})
add_library(easySVA::GB28181Core ALIAS easy_sva_gb28181_core)

set(GB28181_SERVICE_SOURCES
  service/DigestNonceStore.cpp
  service/DigestNonceStore.h
  service/GbSipConfig.cpp
  service/GbSipConfig.h
  service/GbSipRequestProcessor.cpp
  service/GbSipRequestProcessor.h
  service/GbSipTransport.cpp
  service/GbSipTransport.h
  service/RegistrationStore.cpp
  service/RegistrationStore.h
  service/SipRequestProcessor.cpp
  service/SipRequestProcessor.h)

add_library(easy_sva_gb28181_service STATIC ${GB28181_SERVICE_SOURCES})
add_library(easySVA::GB28181Service ALIAS easy_sva_gb28181_service)

target_compile_options(easy_sva_gb28181_core
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_include_directories(easy_sva_gb28181_core
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(easy_sva_gb28181_core
  PUBLIC ZLMediaKit::ToolKit)
target_compile_options(easy_sva_gb28181_service
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_include_directories(easy_sva_gb28181_service
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(easy_sva_gb28181_service
  PUBLIC easySVA::GB28181Core)

if(USE_SOLUTION_FOLDERS)
  set_property(TARGET easy_sva_gb28181_core PROPERTY FOLDER "gb28181")
  set_property(TARGET easy_sva_gb28181_service PROPERTY FOLDER "gb28181")
endif()

add_executable(GbSipServer main.cpp)
target_compile_options(GbSipServer
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_link_libraries(GbSipServer
  PRIVATE easySVA::GB28181Service)

if(USE_SOLUTION_FOLDERS)
  set_property(TARGET GbSipServer PROPERTY FOLDER "gb28181")
endif()

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../conf/gb28181.ini"
     DESTINATION ${EXECUTABLE_OUTPUT_PATH})

if(ENABLE_TESTS)
  add_executable(test_gb28181_sip_core tests/test_sip_core.cpp)
  target_compile_options(test_gb28181_sip_core
    PRIVATE ${COMPILE_OPTIONS_DEFAULT})
  target_link_libraries(test_gb28181_sip_core
    PRIVATE easySVA::GB28181Core)

  add_executable(test_gb28181_service_core tests/test_service_core.cpp)
  target_compile_options(test_gb28181_service_core
    PRIVATE ${COMPILE_OPTIONS_DEFAULT})
  target_link_libraries(test_gb28181_service_core
    PRIVATE easySVA::GB28181Service)

  add_executable(test_gb28181_registration tests/test_registration.cpp)
  target_compile_options(test_gb28181_registration
    PRIVATE ${COMPILE_OPTIONS_DEFAULT})
  target_link_libraries(test_gb28181_registration
    PRIVATE easySVA::GB28181Service)

  if(USE_SOLUTION_FOLDERS)
    set_property(TARGET test_gb28181_sip_core PROPERTY FOLDER "gb28181/test")
    set_property(TARGET test_gb28181_service_core PROPERTY FOLDER "gb28181/test")
    set_property(TARGET test_gb28181_registration PROPERTY FOLDER "gb28181/test")
  endif()
endif()
