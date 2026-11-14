# storm per PC: synth per drone (versione PS1 in originale_psx/).
# Un solo codice C11 per Windows, macOS e Linux: le differenze stanno qui e dentro SDL3/RtMidi.
cmake_minimum_required(VERSION 3.24)

# macOS 11 e' la prima versione per Apple Silicon; va detto prima di project()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

# C++ serve solo per compilare RtMidi
project(storm LANGUAGES C CXX)
# macOS: Objective-C per la finestra del plugin, Objective-C++ per clap-wrapper. I linguaggi vanno
# accesi qui nel progetto principale: accesi in una sottocartella CMake non genera le loro regole
if(APPLE)
	enable_language(OBJC OBJCXX)
endif()

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

# Plugin CLAP e VST3 senza finestra (src/plugin.c). Il VST3 lo fa clap-wrapper sopra il CLAP.
# Tutto scaricato per URL con l'impronta, come SDL3 e RtMidi; il VST3 SDK arriva a pezzi (i suoi
# sottomoduli) nelle cartelle dove clap-wrapper li cerca. SOURCE_SUBDIR inesistente = solo scaricare.
option(STORM_PLUGIN "anche il plugin CLAP e VST3" ON)
if(STORM_PLUGIN)
	set(VST3_TAG v3.8.0_build_66)
	set(VST3_DIR ${FETCHCONTENT_BASE_DIR}/vst3sdk-src)
	FetchContent_Declare(clap
		URL https://github.com/free-audio/clap/archive/refs/tags/1.2.10.tar.gz
		URL_HASH SHA256=58fdb977c6678859c15d57942378b651399178f49a3a8ecad1813b9f231dc096
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_Declare(vst3sdk
		URL https://github.com/steinbergmedia/vst3sdk/archive/refs/tags/${VST3_TAG}.tar.gz
		URL_HASH SHA256=d93c12077d848977d4d397c8ab6721c4b4966223a7707bfe6519aa419c396af4
		SOURCE_DIR ${VST3_DIR} SOURCE_SUBDIR niente DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_Declare(vst3_base
		URL https://github.com/steinbergmedia/vst3_base/archive/refs/tags/${VST3_TAG}.tar.gz
		URL_HASH SHA256=001ef3dccf89505ee624766c8140c6c845fbd214460e02ea16145db29f1e2ed6
		SOURCE_DIR ${VST3_DIR}/base SOURCE_SUBDIR niente DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_Declare(vst3_pluginterfaces
		URL https://github.com/steinbergmedia/vst3_pluginterfaces/archive/refs/tags/${VST3_TAG}.tar.gz
		URL_HASH SHA256=c922f0a35f2c093dfcb24ccd95282e70d00cc2de42fc0c8ac1577398ffe7b6d4
		SOURCE_DIR ${VST3_DIR}/pluginterfaces SOURCE_SUBDIR niente DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_Declare(vst3_public_sdk
		URL https://github.com/steinbergmedia/vst3_public_sdk/archive/refs/tags/${VST3_TAG}.tar.gz
		URL_HASH SHA256=83b64e6666a54a35a2292a73ef35048b6d0dadd5e50a22cb437ac4cfb69ba81f
		SOURCE_DIR ${VST3_DIR}/public.sdk SOURCE_SUBDIR niente DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_Declare(clap-wrapper
		URL https://github.com/free-audio/clap-wrapper/archive/refs/tags/v0.17.0.tar.gz
		URL_HASH SHA256=50acbfc16a55f58b0b56a0aee7f4ebdcccb31ae5b8295cf80d28475b3a3cd43c
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
	FetchContent_MakeAvailable(clap vst3sdk vst3_base vst3_pluginterfaces vst3_public_sdk)
	set(VST3_SDK_ROOT ${VST3_DIR})
	set(CMAKE_CXX_STANDARD 17) # clap-wrapper non compila in C++20 (base di gcc 16); RtMidi e' gia' fatta
	# Tutto quello che va dentro il plugin (una libreria dinamica) deve essere PIC su Linux e macOS:
	# il synth, SDL e le librerie del VST3 che make_clapfirst_plugins crea in questa cartella
	# (clap-wrapper accende PIC solo nella sua). storm (l'exe) e' gia' fatto sopra e non cambia; su
	# Windows non cambia niente.
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	FetchContent_MakeAvailable(clap-wrapper)

	set_target_properties(SDL3-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
	add_library(storm_motore STATIC src/storm.c src/controlli.c src/psx_spu.c src/psx_gpu.c)
	target_include_directories(storm_motore PUBLIC src originale_psx ${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(storm_motore PUBLIC SDL3::SDL3-static clap)
	target_compile_options(storm_motore PRIVATE -Wall -Wextra -ffp-contract=off)
	# La finestra del plugin dentro quella della DAW: l'unico codice diverso per sistema, un file per
	# ognuno (src/finestra.h), scelto qui
	if(WIN32)
		target_sources(storm_motore PRIVATE src/finestra_win.c)
		target_link_libraries(storm_motore PUBLIC user32 gdi32)
	elseif(APPLE)
		target_sources(storm_motore PRIVATE src/finestra_mac.m)
		target_link_libraries(storm_motore PUBLIC "-framework Cocoa" "-framework QuartzCore")
	else()
		find_package(X11 REQUIRED)
		target_sources(storm_motore PRIVATE src/finestra_x11.c)
		target_link_libraries(storm_motore PUBLIC X11::X11)
	endif()
	# plugin.c si compila dentro ogni formato (CLAP, VST3): le opzioni vanno sul file
	set_source_files_properties(src/plugin.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-ffp-contract=off")
	make_clapfirst_plugins(
		TARGET_NAME storm_plugin
		IMPL_TARGET storm_motore
		OUTPUT_NAME storm
		ENTRY_SOURCE src/plugin.c
		BUNDLE_IDENTIFIER storm.synth
		BUNDLE_VERSION 0.1
		COPY_AFTER_BUILD FALSE
		WINDOWS_FOLDER_VST3 TRUE # su Windows la cartella storm.vst3\Contents\x86_64-win\, come vuole lo standard VST3
		PLUGIN_FORMATS CLAP VST3
		ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin)
	if(MINGW) # niente DLL di MSYS2 accanto al plugin, niente "lib" davanti al nome
		target_link_options(storm_plugin_clap PRIVATE -static)
		target_link_options(storm_plugin_vst3 PRIVATE -static)
		set_target_properties(storm_plugin_clap storm_plugin_vst3 PROPERTIES PREFIX "")
	endif()
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
