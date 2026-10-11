package com.starrift.starzip

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import kotlinx.coroutines.launch

class ExplorerActivity : AppCompatActivity() {
    private val model: ExplorerViewModel by viewModels()
    private lateinit var pathView: TextView
    private lateinit var messageView: TextView
    private lateinit var grantButton: Button
    private lateinit var upButton: Button
    private lateinit var list: RecyclerView
    private lateinit var adapter: FileAdapter

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { refreshAccess() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pathView = TextView(this)
        messageView = TextView(this)
        grantButton = Button(this).apply { text = "Grant Access" }
        upButton = Button(this).apply { text = "Up" }
        adapter = FileAdapter(model) { entry -> onEntry(entry) }
        list = RecyclerView(this).apply {
            layoutManager = LinearLayoutManager(this@ExplorerActivity)
            adapter = this@ExplorerActivity.adapter
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(pathView)
            addView(messageView)
            addView(grantButton)
            addView(upButton)
            addView(list)
        })

        grantButton.setOnClickListener { requestAccess() }
        upButton.setOnClickListener { model.goUp() }

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (!model.goUp()) {
                    isEnabled = false
                    onBackPressedDispatcher.onBackPressed()
                }
            }
        })

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                model.state.collect { render(it) }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        refreshAccess()
    }

    private fun hasAccess(): Boolean =
        if (Build.VERSION.SDK_INT >= 30) {
            Environment.isExternalStorageManager()
        } else {
            ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE) ==
                PackageManager.PERMISSION_GRANTED
        }

    private fun requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            startActivity(
                Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + packageName)
                )
            )
        } else {
            permissionLauncher.launch(
                arrayOf(
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                )
            )
        }
    }

    private fun refreshAccess() {
        if (hasAccess()) {
            grantButton.visibility = View.GONE
            if (model.state.value.path.isEmpty()) {
                model.open(Environment.getExternalStorageDirectory().absolutePath)
            }
        } else {
            grantButton.visibility = View.VISIBLE
            messageView.text = "All files access is required"
        }
    }

    private fun onEntry(entry: FileEntry) {
        val base = model.state.value.path
        if (entry.isDir) {
            model.open(File(base, entry.name).absolutePath)
        } else {
            Toast.makeText(this, entry.name + "  " + entry.size + " B", Toast.LENGTH_SHORT).show()
        }
    }

    private fun render(state: ExplorerState) {
        pathView.text = state.path
        messageView.text = when (state.errorCode) {
            0 -> "items " + state.total
            13 -> "Access denied"
            2 -> "Not found"
            20 -> "Not a directory"
            else -> "Error " + state.errorCode
        }
        adapter.notifyDataSetChanged()
    }
}
