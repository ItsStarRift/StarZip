package com.starrift.starzip

import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import java.util.Locale

class FileAdapter(
    private val model: ExplorerViewModel,
    private val onClick: (FileEntry) -> Unit
) : RecyclerView.Adapter<FileAdapter.Holder>() {

    class Holder(val root: LinearLayout, val title: TextView, val detail: TextView) :
        RecyclerView.ViewHolder(root)

    override fun getItemCount(): Int = model.state.value.total.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
        val context = parent.context
        val title = TextView(context).apply { textSize = 16f }
        val detail = TextView(context).apply { textSize = 12f }
        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 20, 32, 20)
            layoutParams = RecyclerView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            addView(title)
            addView(detail)
        }
        return Holder(root, title, detail)
    }

    override fun onBindViewHolder(holder: Holder, position: Int) {
        val entry = model.entryAt(position)
        if (entry == null) {
            holder.title.text = ""
            holder.detail.text = ""
            holder.root.setOnClickListener(null)
            return
        }
        holder.title.text = if (entry.isDir) "[D] " + entry.name else entry.name
        holder.detail.text = if (entry.isDir) "" else formatSize(entry.size)
        holder.root.setOnClickListener { onClick(entry) }
    }

    private fun formatSize(size: Long): String {
        if (size < 1024) return "$size B"
        val units = arrayOf("KB", "MB", "GB", "TB")
        var value = size.toDouble()
        var unit = -1
        while (value >= 1024 && unit < units.size - 1) {
            value /= 1024
            unit++
        }
        return String.format(Locale.US, "%.2f %s", value, units[unit])
    }
}
