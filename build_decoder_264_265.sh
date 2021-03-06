# 编译 FFmpeg

SHELL_FOLDER=$(cd "$(dirname "$0")"; pwd)
echo SHELL_FOLDER : $SHELL_FOLDER

rm -r ${SHELL_FOLDER}/ffmpeg
mkdir -p ${SHELL_FOLDER}/ffmpeg
cd ${SHELL_FOLDER}/../ffmpeg # ffmpeg 的源码所在的目录

emconfigure ./configure --cc="emcc" --cxx="em++" --ar="emar" --prefix="${SHELL_FOLDER}/ffmpeg" \
    --enable-cross-compile --target-os=none --arch=x86_32 --cpu=generic \
    --enable-gpl --enable-version3 \
    --disable-debug --disable-asm --disable-doc \
    --disable-avdevice --disable-swresample --disable-postproc --disable-avfilter \
    --disable-programs --disable-protocols --disable-network \
    --disable-audiotoolbox --disable-videotoolbox \
    --disable-encoders --disable-decoders --disable-muxers --disable-demuxers --disable-parsers \
    --enable-demuxer=h264 --enable-demuxer=hevc \
    --enable-decoder=hevc --enable-parser=hevc \
    --enable-decoder=h264  --enable-parser=h264

# emconfigure ./configure --cc="emcc" --cxx="em++" --ar="emar" --prefix="${SHELL_FOLDER}/ffmpeg" \
#     --enable-cross-compile --target-os=none --arch=x86_32 --cpu=generic \
#     --enable-gpl --enable-version3 \
#     --disable-asm --disable-programs \
#     --disable-avdevice --disable-swresample --disable-avfilter \
#     --disable-encoders --disable-muxers \
#     --enable-demuxer=h264 --enable-demuxer=hevc \
#     --enable-decoder=hevc --enable-parser=hevc \
#     --enable-decoder=h264  --enable-parser=h264

make -j 8
make install

cd ${SHELL_FOLDER}
