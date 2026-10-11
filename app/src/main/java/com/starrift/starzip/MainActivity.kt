package com.starrift.starzip

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.text.InputType
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {
    private var job: Job? = null
    private var sourceUri: Uri? = null
    private var archiveUri: Uri? = null
    private var archiveName = "archive"
    private var sessionId = 0L
    private var diskRemoved = false
    private val nodeStack = ArrayList<Long>()
    private var entries: List<ArchiveEntry> = emptyList()
    private var pendingEntry: ArchiveEntry? = null
    private var volumeCallback: Any? = null
    private lateinit var status: TextView
    private lateinit var bar: ProgressBar
    private lateinit var adapter: ArrayAdapter<String>

    private val callback = object : ProgressCallback {
        override fun onProgress(percentage: Int, currentFileName: String) {
            runOnUiThread {
                bar.progress = percentage
                status.text = "%$percentage  $currentFileName"
            }
        }
    }

    private val diskReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            handleDiskRemoved()
        }
    }

    private val pickCopyTarget = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri -> if (uri != null) runCopy(uri) }

    private val pickSource = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            sourceUri = uri
            pickCopyTarget.launch("copy_test.bin")
        }
    }

    private val pickArchive = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri -> if (uri != null) openArchive(uri, null) }

    private val pickExtractTarget = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri -> if (uri != null) runExtract(uri) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply { text = NativeBridge.nativeVersion() }
        bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100 }
        adapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, ArrayList<String>())
        val list = ListView(this).apply {
            adapter = this@MainActivity.adapter
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        val open = Button(this).apply { text = "Open Archive" }
        val up = Button(this).apply { text = "Up" }
        val cancel = Button(this).apply { text = "Cancel" }
        val start = Button(this).apply { text = "Start Test" }
        val copy = Button(this).apply { text = "Copy File" }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(status); addView(bar)
            addView(open); addView(up); addView(cancel); addView(start); addView(copy)
            addView(list)
        })

        open.setOnClickListener {
            if (job?.isActive != true) pickArchive.launch(arrayOf("*/*"))
        }
        up.setOnClickListener {
            if (job?.isActive != true && sessionId != 0L && nodeStack.size > 1) {
                nodeStack.removeAt(nodeStack.size - 1)
                job = lifecycleScope.launch { showDir() }
            }
        }
        cancel.setOnClickListener {
            NativeBridge.nativeCancelOperation()
            if (sessionId != 0L) NativeBridge.nativeCancelArchive(sessionId)
        }
        start.setOnClickListener {
            if (job?.isActive == true) return@setOnClickListener
            job = lifecycleScope.launch {
                val code = withContext(Dispatchers.IO) {
                    NativeBridge.nativeSelfTest(2048, 4, callback)
                }
                status.text = describe(code)
            }
        }
        copy.setOnClickListener {
            if (job?.isActive != true) pickSource.launch(arrayOf("*/*"))
        }
        list.setOnItemClickListener { _, _, position, _ ->
            if (job?.isActive == true || position >= entries.size) return@setOnItemClickListener
            val entry = entries[position]
            if (entry.isDir) {
                nodeStack.add(entry.nodeId)
                job = lifecycleScope.launch { showDir() }
            } else {
                pendingEntry = entry
                pickExtractTarget.launch(safeName(entry.name))
            }
        }

        registerDiskWatch()
    }

    override fun onDestroy() {
        unregisterDiskWatch()
        closeCurrent()
        super.onDestroy()
    }

    private fun registerDiskWatch() {
        val filter = IntentFilter(Intent.ACTION_MEDIA_EJECT).apply {
            addAction(Intent.ACTION_MEDIA_BAD_REMOVAL)
            addAction(Intent.ACTION_MEDIA_UNMOUNTED)
            addAction(Intent.ACTION_MEDIA_REMOVED)
            addDataScheme("file")
        }
        ContextCompat.registerReceiver(this, diskReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        if (Build.VERSION.SDK_INT >= 30) registerVolumeWatch()
    }

    private fun unregisterDiskWatch() {
        runCatching { unregisterReceiver(diskReceiver) }
        if (Build.VERSION.SDK_INT >= 30) unregisterVolumeWatch()
    }

    @RequiresApi(30)
    private fun registerVolumeWatch() {
        val manager = getSystemService(StorageManager::class.java) ?: return
        val watcher = object : StorageManager.StorageVolumeCallback() {
            override fun onStateChanged(volume: StorageVolume) {
                val state = volume.state
                if (state != Environment.MEDIA_MOUNTED && state != Environment.MEDIA_MOUNTED_READ_ONLY) {
                    handleDiskRemoved()
                }
            }
        }
        volumeCallback = watcher
        manager.registerStorageVolumeCallback(ContextCompat.getMainExecutor(this), watcher)
    }

    @RequiresApi(30)
    private fun unregisterVolumeWatch() {
        val watcher = volumeCallback as? StorageManager.StorageVolumeCallback ?: return
        val manager = getSystemService(StorageManager::class.java) ?: return
        runCatching { manager.unregisterStorageVolumeCallback(watcher) }
        volumeCallback = null
    }

    private fun handleDiskRemoved() {
        val id = sessionId
        if (id == 0L) return
        diskRemoved = true
        NativeBridge.nativeCancelArchive(id)
        closeCurrent()
        entries = emptyList()
        nodeStack.clear()
        adapter.clear()
        bar.progress = 0
        status.text = "Disk removed"
    }

    private fun safeName(name: String): String {
        val cleaned = name.replace('/', '_').replace('\\', '_')
        return if (cleaned.isEmpty() || cleaned == "." || cleaned == "..") "_" else cleaned
    }

    private fun describe(code: Int): String = when (code) {
        NativeBridge.RES_OK -> "Done"
        NativeBridge.RES_CANCELLED -> "Cancelled"
        NativeBridge.RES_PASSWORD -> "Password required"
        NativeBridge.RES_NOT_ARCHIVE -> "Not an archive"
        NativeBridge.RES_DATA -> "Data error or wrong password"
        else -> "Error"
    }

    private fun closeCurrent() {
        if (sessionId != 0L) {
            NativeBridge.nativeCloseArchive(sessionId)
            sessionId = 0L
        }
    }

    private fun queryName(uri: Uri): String =
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { if (it.moveToFirst()) it.getString(0) else null } ?: "archive"

    private fun askPassword(uri: Uri) {
        val edit = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        AlertDialog.Builder(this)
            .setTitle("Password required or wrong")
            .setView(edit)
            .setPositiveButton("OK") { _, _ ->
                val chars = CharArray(edit.text.length)
                edit.text.getChars(0, chars.size, chars, 0)
                edit.text.clear()
                openArchive(uri, chars)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun openArchive(uri: Uri, password: CharArray?) {
        if (job?.isActive == true) {
            password?.fill('\u0000')
            return
        }
        val hadPassword = password != null
        job = lifecycleScope.launch {
            val id = withContext(Dispatchers.IO) {
                try {
                    val name = queryName(uri)
                    archiveName = name
                    contentResolver.openFileDescriptor(uri, "r")!!.use { pfd ->
                        NativeBridge.nativeOpenArchive(pfd.fd, name, password)
                    }
                } catch (e: Exception) {
                    -2L
                }
            }
            password?.fill('\u0000')
            if (id > 0L) {
                closeCurrent()
                diskRemoved = false
                sessionId = id
                archiveUri = uri
                nodeStack.clear()
                nodeStack.add(0L)
                showDir()
            } else if (id == -3L || (hadPassword && id == -4L)) {
                askPassword(uri)
            } else {
                status.text = "Open failed, code " + (-id)
            }
        }
    }

    private suspend fun showDir() {
        val id = sessionId
        if (id == 0L || nodeStack.isEmpty()) return
        val node = nodeStack.last()
        val loaded = withContext(Dispatchers.IO) {
            val result = ArrayList<ArchiveEntry>()
            val total = NativeBridge.nativeDirCount(id, node)
            var offset = 0L
            while (offset < total) {
                val page = NativeBridge.nativeGetItems(id, node, offset, 1000)
                if (page == null || page.isEmpty()) break
                result.addAll(page)
                offset += page.size
            }
            result
        }
        if (id != sessionId) return
        entries = loaded
        adapter.clear()
        adapter.addAll(loaded.map { if (it.isDir) "[D] " + it.name else it.name + "  " + it.size })
        status.text = archiveName + "  level " + (nodeStack.size - 1) + "  items " + loaded.size
        bar.progress = 0
    }

    private fun discardTarget(target: Uri) {
        lifecycleScope.launch {
            withContext(Dispatchers.IO) {
                runCatching { DocumentsContract.deleteDocument(contentResolver, target) }
            }
        }
    }

    private fun askExtractPassword(target: Uri) {
        val edit = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        AlertDialog.Builder(this)
            .setTitle("Password required or wrong")
            .setView(edit)
            .setPositiveButton("OK") { _, _ ->
                val chars = CharArray(edit.text.length)
                edit.text.getChars(0, chars.size, chars, 0)
                edit.text.clear()
                if (sessionId != 0L) NativeBridge.nativeSetPassword(sessionId, chars)
                chars.fill('\u0000')
                runExtract(target)
            }
            .setNegativeButton("Cancel") { _, _ -> discardTarget(target) }
            .setOnCancelListener { discardTarget(target) }
            .show()
    }

    private fun runExtract(target: Uri) {
        val entry = pendingEntry ?: return
        val id = sessionId
        if (id == 0L || job?.isActive == true) {
            discardTarget(target)
            return
        }
        job = lifecycleScope.launch {
            val code = withContext(Dispatchers.IO) {
                try {
                    contentResolver.openFileDescriptor(target, "rwt")!!.use { pfd ->
                        NativeBridge.nativeExtractItem(id, entry.nodeId, pfd.fd, callback)
                    }
                } catch (e: Exception) {
                    NativeBridge.RES_ERROR
                }
            }
            if (diskRemoved) {
                status.text = "Disk removed"
                discardTarget(target)
                return@launch
            }
            status.text = describe(code)
            if (code == NativeBridge.RES_PASSWORD || code == NativeBridge.RES_DATA) {
                askExtractPassword(target)
            } else if (code != NativeBridge.RES_OK) {
                discardTarget(target)
            }
        }
    }

    private fun runCopy(target: Uri) {
        val source = sourceUri ?: return
        job = lifecycleScope.launch {
            val code = withContext(Dispatchers.IO) {
                try {
                    contentResolver.openFileDescriptor(source, "r")!!.use { inPfd ->
                        contentResolver.openFileDescriptor(target, "rwt")!!.use { outPfd ->
                            NativeBridge.nativeCopy(null, inPfd.fd, null, outPfd.fd, 4, callback)
                        }
                    }
                } catch (e: Exception) {
                    NativeBridge.RES_ERROR
                }
            }
            if (code != NativeBridge.RES_OK) {
                withContext(Dispatchers.IO) {
                    runCatching { DocumentsContract.deleteDocument(contentResolver, target) }
                }
            }
            status.text = describe(code)
        }
    }
}
