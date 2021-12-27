# 编译 FFmpeg

SHELL_FOLDER=$(cd "$(dirname "$0")"; pwd)
echo SHELL_FOLDER : $SHELL_FOLDER

rm -r ${SHELL_FOLDER}/ffmpeg
mkdir -p ${SHELL_FOLDER}/ffmpeg
cd ${SHELL_FOLDER}/../ffmpeg # ffmpeg 的源码所在的目录
# make clean
emconfigure ./configure --cc="emcc" --cxx="em++" --ar="emar" --prefix="${SHELL_FOLDER}/ffmpeg" \
    --enable-cross-compile --target-os=none --arch=x86_32 --cpu=generic \
    --enable-gpl --enable-version3 \
    --disable-avdevice --disable-swresample --disable-postproc --disable-avfilter \
    --disable-programs --disable-logging --disable-everything \
    --disable-ffplay --disable-ffprobe --disable-asm --disable-doc --disable-devices --disable-network \
    --disable-hwaccels --disable-parsers --disable-bsfs --disable-debug --disable-protocols --disable-indevs --disable-outdevs \
    --enable-decoder=hevc --enable-parser=hevc \
    --enable-decoder=h264  --enable-parser=h264
make -j 8
make install
cd ${SHELL_FOLDER}
