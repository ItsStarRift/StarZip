package com.starrift.starzip

import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
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
        status = TextView(this).apply { text = NativeBridge.nativeVersion() }
        bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100 }
        val start = Button(this).apply { text = "Start Test" }
        val copy = Button(this).apply { text = "Copy File" }
        val cancel = Button(this).apply { text = "Cancel" }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(status); addView(bar); addView(start); addView(copy); addView(cancel)
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

    private fun showResult(code: Int) {
        status.text = when (code) {
            NativeBridge.RES_OK -> "Done"
            NativeBridge.RES_CANCELLED -> "Cancelled"
            else -> "Error"
        }
    }
}
