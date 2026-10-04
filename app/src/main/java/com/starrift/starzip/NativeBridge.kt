package com.starrift.starzip

object NativeBridge {
    init { System.loadLibrary("starzip_native") }
    external fun nativeVersion(): String
}
