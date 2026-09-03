set(GB28181_CORE_SOURCES
  core/DigestAuth.cpp
  core/DigestAuth.h
  core/SipMessage.cpp
  core/SipMessage.h)

add_library(easy_sva_gb28181_core STATIC ${GB28181_CORE_SOURCES})
add_library(easySVA::GB28181Core ALIAS easy_sva_gb28181_core)

target_compile_options(easy_sva_gb28181_core
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_include_directories(easy_sva_gb28181_core
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(easy_sva_gb28181_core
  PUBLIC ZLMediaKit::ToolKit)

if(USE_SOLUTION_FOLDERS)
  set_property(TARGET easy_sva_gb28181_core PROPERTY FOLDER "gb28181")
endif()

if(ENABLE_TESTS)
  add_executable(test_gb28181_sip_core tests/test_sip_core.cpp)
  target_compile_options(test_gb28181_sip_core
    PRIVATE ${COMPILE_OPTIONS_DEFAULT})
  target_link_libraries(test_gb28181_sip_core
    PRIVATE easySVA::GB28181Core)

  if(USE_SOLUTION_FOLDERS)
    set_property(TARGET test_gb28181_sip_core PROPERTY FOLDER "gb28181/test")
  endif()
endif()
