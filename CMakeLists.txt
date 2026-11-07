# storm per PC: synth per drone (versione PS1 in originale_psx/).
# Un solo codice C11 per Windows, macOS e Linux: le differenze stanno qui e dentro SDL3/RtMidi.
cmake_minimum_required(VERSION 3.21)

# macOS 11 e' la prima versione per Apple Silicon; va detto prima di project()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

# C++ serve solo per compilare RtMidi
project(storm LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# SDL3: release stabile fissa, collegata statica
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
	URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.20.tar.gz
	URL_HASH SHA256=5cb56d4223a41283f3c2baf6f1263fba171692409ae405a50417a7c8e7e28deb
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# RtMidi 6.0.0 (ingresso MIDI), statica
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rtmidi
	URL https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.tar.gz
	URL_HASH SHA256=ef7bcda27fee6936b651c29ebe9544c74959d0b1583b716ce80a1c6fea7617f0
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# MIDI: su Linux solo il sequencer ALSA. JACK spento: l'exe non deve dipendere da libjack.
# Senza ALSA RtMidi compilerebbe senza MIDI e senza errore: meglio fermare la build.
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(ALSA REQUIRED)
endif()

FetchContent_MakeAvailable(SDL3 rtmidi)

# storm.c e' il programma della PS1 (originale_psx/main.c) sopra la PS1 finta (psx_*.c);
# le tabelle generate (onde, font) si leggono da originale_psx/; i testi da src/testi.h (script/testi.py),
# che sta prima negli include
# WIN32: su Windows niente finestra della console; MACOSX_BUNDLE: su macOS storm.app.
# Su Linux le due parole non fanno niente.
add_executable(storm WIN32 MACOSX_BUNDLE src/main.c src/storm.c src/controlli.c src/midi.c src/psx_spu.c src/psx_gpu.c)
target_include_directories(storm PRIVATE src originale_psx ${CMAKE_CURRENT_BINARY_DIR})

# La mappa MIDI predefinita (mappe/storm.txt) dentro l'exe come array C: il programma si consegna da
# solo. Si rigenera quando cambia il file (file(CONFIGURE) riscrive solo se il contenuto e' diverso).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mappe/storm.txt)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/mappe/storm.txt MAPPA_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MAPPA_HEX "${MAPPA_HEX}")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mappa_storm.h
	CONTENT "static const char mappa_storm[] = { ${MAPPA_HEX} 0 };\n")
target_link_libraries(storm PRIVATE SDL3::SDL3-static rtmidi)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	# -ffp-contract=off: niente FMA (a*b+c in un'istruzione sola, arrotondamento diverso),
	# cosi' i float danno gli stessi bit su x86_64 e arm64
	target_compile_options(storm PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

if(MINGW)
	# exe unico: niente DLL di MSYS2 (libgcc, libstdc++, winpthread)
	target_link_options(storm PRIVATE -static)
endif()

if(APPLE)
	# Info.plist dal modello di CMake; firma ad-hoc (senza account Apple) dopo ogni build
	set_target_properties(storm PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME storm
		MACOSX_BUNDLE_GUI_IDENTIFIER storm.synth
		MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1
		MACOSX_BUNDLE_BUNDLE_VERSION 0.1)
	add_custom_command(TARGET storm POST_BUILD
		COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:storm>")
endif()
