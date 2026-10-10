package com.starrift.starzip

data class ArchiveEntry(
    val nodeId: Long,
    val size: Long,
    val isDir: Boolean,
    val name: String
)
