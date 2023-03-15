#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
CFLAGS += -O3 -DLINKALL -DLOOPBACK -DNO_FAAD -DRESAMPLE16 -DEMBEDDED -DTREMOR_ONLY -DBYTES_PER_FRAME=4 	\
	-I$(COMPONENT_PATH)/../codecs/inc			\
	-I$(COMPONENT_PATH)/../codecs/inc/mad 		\
	-I$(COMPONENT_PATH)/../codecs/inc/alac		\
	-I$(COMPONENT_PATH)/../codecs/inc/helix-aac	\
	-I$(COMPONENT_PATH)/../codecs/inc/vorbis 	\
	-I$(COMPONENT_PATH)/../codecs/inc/soxr 		\
	-I$(COMPONENT_PATH)/../codecs/inc/resample16	\
	-I$(COMPONENT_PATH)/../tools				\
	-I$(COMPONENT_PATH)/../codecs/inc/opus 		\
	-I$(COMPONENT_PATH)/../codecs/inc/opusfile	\
	-I$(COMPONENT_PATH)/../driver_bt			\
	-I$(COMPONENT_PATH)/../raop					\
	-I$(COMPONENT_PATH)/../services				\
	-I$(COMPONENT_PATH)/../audio/inc

#	-I$(COMPONENT_PATH)/../codecs/inc/faad2

COMPONENT_SRCDIRS := . tas57xx ac101 external wm8978 wm8711bl
COMPONENT_ADD_INCLUDEDIRS := . ./tas57xx ./ac101
COMPONENT_EMBED_FILES := vu.data

