# 编译最终的wasm产出

SHELL_FOLDER=$(cd "$(dirname "$0")"; pwd)

cd ${SHELL_FOLDER}

mkdir -p ${SHELL_FOLDER}/dist

rm -rf dist/libdecoder_264_265.*
export TOTAL_MEMORY=128MB
export EXPORTED_FUNCTIONS="[ \
		'_enableLog', \
		'_disableLog', \
		'_createH264Decoder', \
		'_createH265Decoder', \
		'_releaseDecoder', \
		'_getFrame' \
]"

echo "Running Emscripten..."
emcc src/decoder.c ffmpeg/lib/libavformat.a ffmpeg/lib/libavcodec.a ffmpeg/lib/libavutil.a ffmpeg/lib/libswscale.a \
    -Os \
    -I "ffmpeg/include" \
    -s WASM=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
   	-s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
   	-s EXTRA_EXPORTED_RUNTIME_METHODS="['addFunction']" \
	-s RESERVED_FUNCTION_POINTERS=14 \
	-s FORCE_FILESYSTEM=1 \
	-s SINGLE_FILE=1 \
    -o ${SHELL_FOLDER}/dist/libdecoder_264_265.js

echo "export default Module" >> ${SHELL_FOLDER}/dist/libdecoder_264_265.js

echo "Finished Build"
