import libDe from './dist/libdecoder_264_265'

class Decoder {
  constructor(typ) {
    switch (typ) {
      case 'h264':
        break
      case 'h265':
        break
      default:
        throw new Error('not support type:', typ)
    }
    console.log('xxx')
    console.log('libDe:', libDe)
  }
}

export default Decoder
