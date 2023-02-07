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
		'_putBuffer', \
		'_getFrame', \
		'_getFrameMT', \
		'_findStreamInfo', \
		'_streamInfoReady' \
]"

# FLAGS=' -O0 '
FLAGS=' -Os '
FLAGS=${FLAGS}' -s ASSERTIONS=1 '

echo "Running Emscripten..."
emcc src/decoder2.c ffmpeg/lib/libavformat.a ffmpeg/lib/libavcodec.a ffmpeg/lib/libavutil.a ffmpeg/lib/libswscale.a \
    ${FLAGS} \
    -I "ffmpeg/include" \
    -s WASM=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
		-s WASM_MEM_MAX=4096MB \
    -s ALLOW_MEMORY_GROWTH=1 \
   	-s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
   	-s EXTRA_EXPORTED_RUNTIME_METHODS="['addFunction']" \
	-s RESERVED_FUNCTION_POINTERS=14 \
	-s FORCE_FILESYSTEM=1 \
	-s SINGLE_FILE=1 \
	-s USE_PTHREADS=1 \
	-s PTHREAD_POOL_SIZE=8 \
    -o ${SHELL_FOLDER}/dist/libdecoder_264_265.js

# 替换worker文件的路径
sed -e 's/libdecoder_264_265\.worker\.js/\/wasm_worker\/libdecoder_264_265\.worker\.js/g' -i '' dist/libdecoder_264_265.js

cp ${SHELL_FOLDER}/dist/libdecoder_264_265.js ${SHELL_FOLDER}/dist/libdecoder_264_265.org.js

echo "export default Module" >> ${SHELL_FOLDER}/dist/libdecoder_264_265.js

echo "Finished Build"
