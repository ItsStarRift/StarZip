package com.starrift.starzip

import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {
    private var job: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val status = TextView(this).apply { text = NativeBridge.nativeVersion() }
        val bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100 }
        val start = Button(this).apply { text = "Start Test" }
        val cancel = Button(this).apply { text = "Cancel" }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(status); addView(bar); addView(start); addView(cancel)
        })

        start.setOnClickListener {
            if (job?.isActive == true) return@setOnClickListener
            job = lifecycleScope.launch {
                val code = withContext(Dispatchers.IO) {
                    NativeBridge.nativeSelfTest(2048, 4, object : ProgressCallback {
                        override fun onProgress(percentage: Int, currentFileName: String) {
                            runOnUiThread {
                                bar.progress = percentage
                                status.text = "%$percentage  $currentFileName"
                            }
                        }
                    })
                }
                status.text = when (code) {
                    NativeBridge.RES_OK -> "Done"
                    NativeBridge.RES_CANCELLED -> "Cancelled"
                    else -> "Error"
                }
            }
        }
        cancel.setOnClickListener { NativeBridge.nativeCancelOperation() }
    }
}
