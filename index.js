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

  // ???????????????????????????????????????????????????????????????????????????????????????????????????
  static setReadyCb(cb) {
    if (gReady) {
      setTimeout(cb, 0);
    } else {
      gReadyCbs.push(cb)
    }
  }

  // ?????????????????????
  static isReady() {
    return gReady
  }

  // ????????????????????????????????????
  constructor(typ) {
    const self = this
    this._buf = []
    this._initBuf = []
    this._infoReady = false
    switch (typ) {
      case 'h264':
        {
          const cb = libDe.addFunction((opaque, buf, bufSize) => {
            return self._cb(opaque, buf, bufSize)
          })
          this._ctx = libDe._createH264Decoder(cb)
          this._typ = 'h264'
        }
        break
      case 'h265':
        {
          const cb = libDe.addFunction((opaque, buf, bufSize) => {
            return self._cb(opaque, buf, bufSize)
          })
          this._ctx = libDe._createH265Decoder(cb)
          this._typ = 'h265'
        }
        break
      default:
        throw new Error('not support type:', typ)
    }
    if (!this._ctx) {
      throw new Error('createDecoder fail:', typ)
    }
    log('debug', 'Decoder constructored', typ)
  }

  _cb(opaque, buf, bufSize) {
    // log('debug', 'cb this:', this)
    // log('debug', 'io_cb:', opaque, buf, bufSize);
    let ret = -(0x20464F45) // 'EOF '
    if (this._buf.length <= 0) {
      log('debug', 'no buf when io_cb')
      return ret 
    }
    let item = this._buf.shift()
    if (bufSize >= item.length) {
      libDe.HEAPU8.set(item, buf)
      log('debug', 'io_cb ret 1:', item.length)
      ret = item.length
    } else {
      const restItem = item.subarray(bufSize) // ???????????????????????????
      item = item.subarray(0, bufSize) // ???????????????
      libDe.HEAPU8.set(item, buf)
      this._buf.unshift(restItem)
      log('debug', 'io_cb ret 2:', bufSize)
      ret = bufSize
    }
    if (!this._infoReady) {
      this._initBuf.push(item) // ??????????????????info????????????????????????????????????
    }
    return ret
  }

  type() {
    return this._typ
  }

  // ???????????????????????????
  dispose() {
    if (!this._ctx) {
      return
    }
    libDe._releaseDecoder(this._ctx)
    this._ctx = null
    this._typ = ''
  }

  // ???????????????????????????????????????????????????????????????
  // ????????? Uint8Array ??????
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
    this._buf.push(buf)
    return
  }

  // ?????????
  get() {
    if (!this._ctx) {
      log('error', 'no _ctx when put')
      return null
    }
    if (!this._infoReady) {
      const r = libDe._streamInfoReady(this._ctx)
      if (r) {
        this._infoReady = true
      } else {
        // ???initBuf??????
        for (let i = this._initBuf.length - 1; i >= 0; i--) {
          this._buf.unshift(this._initBuf[i])
        }
        this._initBuf = []
        libDe._findStreamInfo(this._ctx)
        const rr = libDe._streamInfoReady(this._ctx)
        if (rr) {
          this._infoReady = true
          log('info', 'infoReady')
        } else {
          return null
        }
      }
    }
    const f = libDe._getFrame(this._ctx)
    if (f) {
      const widthBuf = libDe.HEAPU8.subarray(f, f + 4)
      const heightBuf = libDe.HEAPU8.subarray(f + 4, f + 8)
      const width = buf2int(widthBuf)
      const height = buf2int(heightBuf)
      // log('info', 'width:', width, ', height:', height)
      // const width = f[0] | f[1]
      const dataSize = (width * height) << 2
      // log('error', 'dataSize:', dataSize)
      const data = new Uint8Array(libDe.HEAPU8.subarray(f + 8, f + 8 + dataSize))
      libDe._free(f)
      return {
        width, 
        height,
        data
      }
    } else {
      log('info', '_getFrame null')
      return null
    }
  }

}

function buf2int(buf) {
  return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24)
}

export default Decoder
