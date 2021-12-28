# 简介
视频解码，使用wasm，调用ffmpeg库。  

# 依赖
* [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)  
* [FFmpeg](https://ffmpeg.org/download.html#get-sources)  

# 构建
./build.sh

# 使用

## 安装
```shell
npm i video-decoder
```
## 使用
```js
import Decoder from 'video-decoder'

Decoder.setReadyCb(() => {
    const de = new Decoder('h265')    // 可选 h264/h265
    de.put(buf)     // buf 需要是 Uint8Array 类型
    // get: 取出一帧数据（如果有的话, 否则返回 null ）
    // 一个对象，包含 width、height、data
    // 其中 data 是 Uint8Array 类型，RGBA，可以直接复制到 ImageData 对象中
    de.get()        
    de.dispose()    // 不再使用后要释放资源
})
```
## 示例
见 [示例项目](https://github.com/zhaohuijun/video-decoder-test)

# 参考项目
* [WasmVideoPlayer](https://github.com/sonysuqin/WasmVideoPlayer)  
* [decoder_wasm](https://github.com/goldvideo/decoder_wasm)  
* [ffmpeg.wasm](https://github.com/ffmpegwasm/ffmpeg.wasm)  
* [YUV-Webgl-Video-Player](https://github.com/p4prasoon/YUV-Webgl-Video-Player)  
