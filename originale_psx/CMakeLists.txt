# storm: synth per drone PS1 PAL (vedi DESIGN.md).
cmake_minimum_required(VERSION 3.21)

project(storm LANGUAGES C CXX ASM)

psn00bsdk_add_executable(storm GPREL main.c)

# -DNTSC=ON: versione NTSC (60 Hz, 320x240, licenza americana)
option(NTSC "versione NTSC" OFF)
if(NTSC)
	target_compile_definitions(storm PRIVATE NTSC=1)
	set(LICENZA license_ntsc.dat)
else()
	set(LICENZA license_pal.dat)
endif()

psn00bsdk_add_cd_image(iso storm iso.xml DEPENDS storm)
