SHELL_FOLDER=$(dirname "$0")

cd ${SHELL_FOLDER}

mkdir -p ${SHELL_FOLDER}/dist

rm -rf dist/libdecoder_264_265.*
export TOTAL_MEMORY=67108864
export EXPORTED_FUNCTIONS="[ \
		'_enableLog', \
		'_disableLog', \
		'_createH264Decoder', \
		'_createH265Decoder', \
		'_releaseDecoder', \
		'_put', \
		'_flush' \
]"

echo "Running Emscripten..."
emcc src/decoder.c ffmpeg/lib/libavcodec.a ffmpeg/lib/libavutil.a ffmpeg/lib/libswscale.a \
    -O2 \
    -I "ffmpeg/include" \
    -s WASM=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
   	-s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
   	-s EXTRA_EXPORTED_RUNTIME_METHODS="['addFunction']" \
	-s RESERVED_FUNCTION_POINTERS=14 \
	-s FORCE_FILESYSTEM=1 \
    -o dist/libdecoder_264_265.wasm

echo "Finished Build"
