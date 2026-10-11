package com.starrift.starzip

data class FileEntry(
    val name: String,
    val size: Long,
    val isDir: Boolean,
    val modified: Long
)
