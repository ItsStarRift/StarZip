package com.starrift.starzip

import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {
    private var job: Job? = null
    private var sourceUri: Uri? = null
    private lateinit var status: TextView
    private lateinit var bar: ProgressBar

    private val callback = object : ProgressCallback {
        override fun onProgress(percentage: Int, currentFileName: String) {
            runOnUiThread {
                bar.progress = percentage
                status.text = "%$percentage  $currentFileName"
            }
        }
    }

    private val pickTarget = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri -> if (uri != null) runCopy(uri) }

    private val pickArchive = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri -> if (uri != null) runOpen(uri) }

    private val pickSource = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            sourceUri = uri
            pickTarget.launch("copy_test.bin")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply { text = NativeBridge.nativeVersion() + "\n" + NativeBridge.nativeSevenZipInfo() }
        bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100 }
        val start = Button(this).apply { text = "Start Test" }
        val copy = Button(this).apply { text = "Copy File" }
        val open = Button(this).apply { text = "Open Archive" }
        val cancel = Button(this).apply { text = "Cancel" }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(status); addView(bar); addView(start); addView(copy); addView(open); addView(cancel)
        })

        start.setOnClickListener {
            if (job?.isActive == true) return@setOnClickListener
            job = lifecycleScope.launch {
                val code = withContext(Dispatchers.IO) {
                    NativeBridge.nativeSelfTest(2048, 4, callback)
                }
                showResult(code)
            }
        }
        copy.setOnClickListener {
            if (job?.isActive != true) pickSource.launch(arrayOf("*/*"))
        }
        open.setOnClickListener {
            if (job?.isActive != true) pickArchive.launch(arrayOf("*/*"))
        }
        cancel.setOnClickListener { NativeBridge.nativeCancelOperation() }
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
            showResult(code)
        }
    }

    private fun runOpen(uri: Uri) {
        job = lifecycleScope.launch {
            val text = withContext(Dispatchers.IO) {
                try {
                    val name = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                        ?.use { if (it.moveToFirst()) it.getString(0) else null } ?: "archive"
                    contentResolver.openFileDescriptor(uri, "r")!!.use { pfd ->
                        val id = NativeBridge.nativeOpenArchive(pfd.fd, name)
                        if (id <= 0) "Open failed, code " + (-id)
                        else {
                            val count = NativeBridge.nativeArchiveItemCount(id)
                            val rootCount = NativeBridge.nativeDirCount(id, 0L)
                            val first = NativeBridge.nativeGetItems(id, 0L, 0L, 8)
                            val names = first?.joinToString("\n") { (if (it.isDir) "[D] " else "    ") + it.name + "  " + it.size } ?: "null"
                            NativeBridge.nativeCloseArchive(id)
                            "Items: " + count + "\nRoot: " + rootCount + "\n" + names
                        }
                    }
                } catch (e: Exception) {
                    "Error"
                }
            }
            status.text = text
        }
    }

    private fun showResult(code: Int) {
        status.text = when (code) {
            NativeBridge.RES_OK -> "Done"
            NativeBridge.RES_CANCELLED -> "Cancelled"
            else -> "Error"
        }
    }
}
