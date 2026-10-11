package com.starrift.starzip

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class ExplorerState(
    val path: String = "",
    val handle: Long = 0L,
    val total: Long = 0L,
    val errorCode: Int = 0,
    val version: Int = 0
)

class ExplorerViewModel : ViewModel() {
    private val mutableState = MutableStateFlow(ExplorerState())
    val state: StateFlow<ExplorerState> = mutableState

    private val pageSize = 200
    private val pages = HashMap<Int, Array<FileEntry>>()
    private var generation = 0
    private var rootPath = ""

    fun open(path: String) {
        if (rootPath.isEmpty()) rootPath = path
        val ticket = ++generation
        viewModelScope.launch {
            val handle = withContext(Dispatchers.IO) { NativeBridge.nativeListDir(path) }
            if (handle > 0L) {
                val total = withContext(Dispatchers.IO) { NativeBridge.nativeDirTotal(handle) }
                if (ticket != generation || total < 0L) {
                    NativeBridge.nativeCloseDir(handle)
                    return@launch
                }
                val old = mutableState.value
                pages.clear()
                mutableState.value = ExplorerState(path, handle, total, 0, old.version + 1)
                if (old.handle != 0L) NativeBridge.nativeCloseDir(old.handle)
            } else if (ticket == generation) {
                val old = mutableState.value
                mutableState.value = old.copy(errorCode = (-handle).toInt(), version = old.version + 1)
            }
        }
    }

    fun goUp(): Boolean {
        val current = mutableState.value.path
        if (current.isEmpty() || current == "/" || current == rootPath) return false
        val parent = File(current).parent ?: return false
        open(parent)
        return true
    }

    fun entryAt(position: Int): FileEntry? {
        val current = mutableState.value
        if (current.handle == 0L || position < 0 || position >= current.total) return null
        val page = position / pageSize
        var array = pages[page]
        if (array == null) {
            array = NativeBridge.nativeDirPage(current.handle, page.toLong() * pageSize, pageSize) ?: return null
            pages[page] = array
        }
        val index = position - page * pageSize
        return if (index < array.size) array[index] else null
    }

    override fun onCleared() {
        val handle = mutableState.value.handle
        if (handle != 0L) NativeBridge.nativeCloseDir(handle)
        super.onCleared()
    }
}
