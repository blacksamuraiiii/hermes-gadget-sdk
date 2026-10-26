# Portable device core. Builds as an ESP-IDF component or a plain CMake library.
set(HG_CORE_SOURCES
    src/app.cpp
    src/app_settings.cpp
    src/app_power.cpp
    src/app_perf.cpp
    src/app_phone.cpp
    src/canvas.cpp
    src/crypto.cpp
    src/font_ascii.cpp
    src/font_cjk.cpp
    src/i18n.cpp
    src/i18n_es.cpp
    src/frame_stats.cpp
    src/json.cpp
    src/mascot_data.cpp
    src/protocol.cpp
    src/qr.cpp
    src/setup.cpp
    src/touch.cpp
    src/ui.cpp
    src/vad.cpp
    src/wifi_networks.cpp
    # Vendored QR encoder (MIT, Richard Moore / Project Nayuki). Adapted for this
    # project; see NOTICE. Committed as .cpp because the project is CXX-only.
    third_party/qrcode/qrcode.cpp
)

# Which font a build carries: the 12 px CJK table (boards that report charset=utf-8)
# or the upstream 5x7 Latin-1 one. Stage 1 keeps the CJK table everywhere, as before;
# the per-board Kconfig switch lands next, together with the 5x7 data file.
set(HG_FONT_CJK_DEFAULT 1)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${HG_CORE_SOURCES} INCLUDE_DIRS include third_party/qrcode)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC HG_FONT_CJK=${HG_FONT_CJK_DEFAULT})
  return()
endif()

add_library(hg_core STATIC ${HG_CORE_SOURCES})
target_include_directories(hg_core PUBLIC include)
target_compile_definitions(hg_core PUBLIC HG_FONT_CJK=${HG_FONT_CJK_DEFAULT})
target_include_directories(hg_core PRIVATE third_party/qrcode)
target_compile_features(hg_core PUBLIC cxx_std_17)
set_target_properties(hg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(hg_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(hg_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
