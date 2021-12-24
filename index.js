import libDe from './dist/libdecoder_264_265'

libDe.postRun = readyCb

const LOG_LEVEL_PANIC = 0
const LOG_LEVEL_FATAL = 8
const LOG_LEVEL_ERROR = 16
const LOG_LEVEL_WARNING = 24
const LOG_LEVEL_INFO = 32
const LOG_LEVEL_VERBOSE = 40
const LOG_LEVEL_DEBUG = 48
const LOG_LEVEL_TRACE = 56

let gLogLevel = -1

function logLevelToInt(level) {
  let l = -1
  switch (level) {
    case 'panic':
      l = LOG_LEVEL_PANIC
      break
    case 'fatal':
      l = LOG_LEVEL_FATAL
      break
    case 'error':
    case 'err':
      l = LOG_LEVEL_ERROR
      break
    case 'warning':
    case 'warn':
      l = LOG_LEVEL_WARNING
      break
    case 'info':
      l = LOG_LEVEL_INFO
      break
    case 'verbose':
      l = LOG_LEVEL_VERBOSE
      break
    case 'debug':
      l = LOG_LEVEL_DEBUG
      break
    case 'trace':
      l = LOG_LEVEL_TRACE
      break
    default:
      break
  }
  return l
}

function logLevelStr(level) {
	switch (level) {
		case LOG_LEVEL_PANIC:
			return "[panic]"
		case LOG_LEVEL_FATAL:
			return "[fatal]"
		case LOG_LEVEL_ERROR:
			return "[err]"
		case LOG_LEVEL_WARNING:
			return "[warn]"
		case LOG_LEVEL_INFO:
			return "[info]"
		case LOG_LEVEL_VERBOSE:
			return "[verbose]"
		case LOG_LEVEL_DEBUG:
			return "[debug]"
		case LOG_LEVEL_TRACE:
			return "[trace]"
	}
	return "[]"

}

function log(level, _) {
  if (gLogLevel < 0) {
    return
  }
  const l = logLevelToInt(level)
  if (l < 0) {
    l = LOG_LEVEL_INFO
  }
  if (l > gLogLevel) {
    return
  }
  const args = Object.values(arguments).slice(1)
  args.unshift(logLevelStr(l))
  if (l > LOG_LEVEL_WARNING) {
    console.log.apply(console, args)
  } else if (l === LOG_LEVEL_WARNING) {
    console.warn.apply(console, args)
  } else {
    console.error.apply(console, args)
  }
}

const gReadyCbs = []
let gReady = false

function readyCb() {
  gReady = true
  for (const cb of gReadyCbs) {
    setTimeout(cb, 0)
  }
}

class Decoder {

  static setLogLevel(level) {
    const l = logLevelToInt(level)
    gLogLevel = l
    log('debug', 'libDe:', libDe)
    if (l < 0) {
      libDe._disableLog()
    } else {
      libDe._enableLog(l)
    }
  }

  // 设置编码器初始化的回调，初始化完毕后才能进行后续操作，包括创建对象
  static setReadyCb(cb) {
    if (gReady) {
      setTimeout(cb, 0);
    } else {
      gReadyCbs.push(cb)
    }
  }

  // 是否已经准备好
  static isReady() {
    return gReady
  }

  // 构造函数，参数是编码类型
  constructor(typ) {
    switch (typ) {
      case 'h264':
        {
          const cb = libDe.addFunction(this._cb)
          this._ctx = libDe._createH264Decoder(cb)
          this._typ = 'h264'
        }
        break
      case 'h265':
        {
          const cb = libDe.addFunction(this._cb)
          this._ctx = libDe._createH265Decoder(cb)
          this._typ = 'h265'
        }
        break
      default:
        throw new Error('not support type:', typ)
    }
  }

  _cb(rgba, width, height) {
    console.log('cb:', rgba, width, height);
  }

  type() {
    return this._typ
  }

  // 析构函数，是否资源
  dispose() {
    if (!this._ctx) {
      return
    }
    libDe._releaseDecoder(this._ctx)
    this._ctx = null
    this._typ = ''
  }

  // 输入编码数据，输出解码结果，输出是一个数组
  // 参数是 Uint8Array 类型
  put(buf) {
    if (!this._ctx) {
      log('error', 'no _ctx when put')
      return
    }
    if (!buf) {
      log('error', 'need param buf')
      return
    }
    if (!(buf instanceof Uint8Array)) {
      log('error', 'param buf must be Uint8Array')
      return
    }
    const cBuf = libDe._malloc(buf.length)
    libDe.HEAPU8.set(buf, cBuf)
    libDe._put(this._ctx, cBuf, buf.length)
    return
  }

  // 确保所有输入的数据都完成解码
  flush() {
    if (!this._ctx) {
      log('error', 'no _ctx when flush')
      return
    }
    libDe._flush(this._ctx);
  }

}

export default Decoder
