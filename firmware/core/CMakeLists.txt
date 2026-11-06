# Portable device core. Builds as an ESP-IDF component or a plain CMake library.
option(HG_CORE_FONT_CJK "Carry the CJK font tables (otherwise the upstream 5x7 Latin-1 one)" ON)
set(HG_CORE_FONT_CELL "12" CACHE STRING "CJK cell height in pixels (12 baseline, 16 for the moji board)")

set(HG_CORE_SOURCES
    src/app.cpp
    src/app_settings.cpp
    src/app_power.cpp
    src/app_perf.cpp
    src/app_phone.cpp
    src/canvas.cpp
    src/crypto.cpp
    src/i18n.cpp
    src/i18n_es.cpp
    src/frame_stats.cpp
    src/json.cpp
    src/mascot_data.cpp
    src/protocol.cpp
    src/qr.cpp
    src/robot_face.cpp
    src/setup.cpp
    src/touch.cpp
    src/ui.cpp
    src/vad.cpp
    src/wake.cpp
    src/wifi_networks.cpp
    # Vendored QR encoder (MIT, Richard Moore / Project Nayuki). Adapted for this
    # project; see NOTICE. Committed as .cpp because the project is CXX-only.
    third_party/qrcode/qrcode.cpp
)

# A build carries exactly one font, and it decides both the metrics and whether the
# device reports charset=utf-8. The board's Kconfig picks it on ESP targets; a host
# build uses the option above (the tests exercise the CJK table by default).
if(ESP_PLATFORM)
  set(HG_FONT_CJK_BUILD ${CONFIG_HG_FONT_CJK})
else()
  set(HG_FONT_CJK_BUILD ${HG_CORE_FONT_CJK})
endif()

# Which CJK cell height this build carries: 12 px is the tree's baseline, 16 px is the
# moji board's (8-column Montserrat ASCII + 16x16 Noto Sans SC, see
# tools/make_cjk_font.py). The tables live in src/font_*<suffix>.cpp.
if(ESP_PLATFORM)
  set(HG_FONT_CELL ${CONFIG_HG_FONT_CELL})
else()
  set(HG_FONT_CELL ${HG_CORE_FONT_CELL})
endif()
if(NOT HG_FONT_CELL)
  set(HG_FONT_CELL 12)
endif()

if(HG_FONT_CJK_BUILD)
  if(HG_FONT_CELL EQUAL 16)
    list(APPEND HG_CORE_SOURCES src/font_ascii_16.cpp src/font_cjk_16.cpp)
  else()
    list(APPEND HG_CORE_SOURCES src/font_ascii.cpp src/font_cjk.cpp)
  endif()
  set(HG_FONT_CJK 1)
else()
  list(APPEND HG_CORE_SOURCES src/font_5x7.cpp)
  set(HG_FONT_CJK 0)
endif()

if(ESP_PLATFORM)
  idf_component_register(SRCS ${HG_CORE_SOURCES} INCLUDE_DIRS include third_party/qrcode)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC HG_FONT_CJK=${HG_FONT_CJK} HG_FONT_PX=${HG_FONT_CELL})
  # Kconfig: Wake word bring-up diagnostics. Core is portable and sees no Kconfig on a host
  # build, so the option reaches it as a define; a release build leaves it unset.
  if(CONFIG_HG_WAKE_DIAG)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC HG_WAKE_DIAG=1)
  endif()
  return()
endif()

add_library(hg_core STATIC ${HG_CORE_SOURCES})
target_include_directories(hg_core PUBLIC include)
target_compile_definitions(hg_core PUBLIC HG_FONT_CJK=${HG_FONT_CJK} HG_FONT_PX=${HG_FONT_CELL})
target_include_directories(hg_core PRIVATE third_party/qrcode)
target_compile_features(hg_core PUBLIC cxx_std_17)
set_target_properties(hg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(hg_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(hg_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
