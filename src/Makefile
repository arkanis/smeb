# Setup implicit rule to build object files
# Disable the unused-parameter warning because we use a common function pointer type
# for connection handling functions. Simple functions don't use all parameters but need
# them in the signature. Better than putting fake assignments in the code.
CC = gcc
CFLAGS = -std=gnu99 -Werror -Wall -Wextra -Wno-unused-parameter

# Use a simply expanded variable to avoid endless recursion in value substitution, see
# http://www.gnu.org/savannah-checkouts/gnu/make/manual/html_node/Flavors.html
CFLAGS := $(CFLAGS) -g
#CFLAGS := $(CFLAGS) -O3


#
# Real applications, object files are created by implicit rules
#

smeb: CPPFLAGS = -I/home/steven/Software/video/ffmpeg
smeb: LDLIBS = \
	~/Software/video/ffmpeg/libavformat/libavformat.a \
	~/Software/video/ffmpeg/libavcodec/libavcodec.a \
	~/Software/video/ffmpeg/libavutil/libavutil.a \
	~/Software/video/libvpx/libvpx.a \
	~/Software/video/x264/libx264.a \
	-pthread -lm -lz -lfaac `pkg-config --libs vorbis libva theora theoraenc vorbisenc`
smeb: client.o ebml_writer.o array.o hash.o list.o base64.o

smebs: smeb
	strip smeb

#
# Tests
#

.PHONY: tests
tests:  tests/ebml_writer_test tests/base64_test
	./tests/ebml_writer_test
	./tests/base64_test

tests/ebml_writer_test: tests/testing.o ebml_writer.o
tests/ebml_reader_test: tests/testing.o ebml_reader.o ebml_writer.o array.o
tests/base64_test:      tests/testing.o base64.o

#
# Playground stuff
#

playground/ebml: ebml_writer.o

playground/ebml_decode: array.o ebml_reader.o

playground/ffdemuxer: CPPFLAGS = -I/home/steven/Software/video/ffmpeg
playground/ffdemuxer: LDLIBS = \
	~/Software/video/ffmpeg/libavformat/libavformat.a \
	~/Software/video/ffmpeg/libavcodec/libavcodec.a \
	~/Software/video/ffmpeg/libavutil/libavutil.a \
	~/Software/video/libvpx/libvpx.a \
	~/Software/video/x264/libx264.a \
	-pthread -lm -lz -lfaac `pkg-config --libs vorbis libva theora theoraenc vorbisenc`
playground/ffdemuxers: playground/ffdemuxer
	strip playground/ffdemuxer


# clean all files in the .gitignore list, ensures that the ignore file
# is properly maintained.
clean:
	rm -fr `tr '\n' ' ' < .gitignore`