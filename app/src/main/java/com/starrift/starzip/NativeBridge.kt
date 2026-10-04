package com.starrift.starzip

interface ProgressCallback {
    fun onProgress(percentage: Int, currentFileName: String)
}

object NativeBridge {
    const val RES_OK = 0
    const val RES_CANCELLED = 1
    const val RES_ERROR = 2

    init { System.loadLibrary("starzip_native") }

    external fun nativeVersion(): String
    external fun nativeSevenZipInfo(): String
    external fun nativeCancelOperation()
    external fun nativeSelfTest(totalMb: Int, chunkMb: Int, callback: ProgressCallback): Int
    external fun nativeCopy(inPath: String?, inFd: Int, outPath: String?, outFd: Int, chunkMb: Int, callback: ProgressCallback): Int
}
