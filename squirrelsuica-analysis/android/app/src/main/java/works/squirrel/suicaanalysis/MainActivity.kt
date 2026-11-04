package works.squirrel.suicaanalysis

import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.tech.NfcF
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.tabs.TabLayout
import works.squirrel.suicaanalysis.databinding.ActivityMainBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Debug/analysis FeliCa reader: dumps everything a card answers without keys,
 * shows a summary and the raw command log, and exports the JSON dump.
 */
class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var nfcAdapter: NfcAdapter? = null
    private val nfcExecutor: ExecutorService = Executors.newSingleThreadExecutor()
    private val reading = AtomicBoolean(false)
    private var dump: CardDump? = null
    private var showLog = false

    private val saveLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
            val json = currentJson() ?: return@registerForActivityResult
            if (uri == null) return@registerForActivityResult
            try {
                contentResolver.openOutputStream(uri)?.use { it.write(json.toByteArray(Charsets.UTF_8)) }
                Toast.makeText(this, R.string.saved, Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(this, getString(R.string.save_failed) + ": ${e.message}", Toast.LENGTH_LONG).show()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        nfcAdapter = NfcAdapter.getDefaultAdapter(this)

        binding.tabs.addTab(binding.tabs.newTab().setText(R.string.tab_summary))
        binding.tabs.addTab(binding.tabs.newTab().setText(R.string.tab_log))
        binding.tabs.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab) {
                showLog = tab.position == 1
                render()
            }
            override fun onTabUnselected(tab: TabLayout.Tab) {}
            override fun onTabReselected(tab: TabLayout.Tab) {}
        })

        binding.btnShare.setOnClickListener {
            val json = currentJson() ?: return@setOnClickListener
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "application/json"
                putExtra(Intent.EXTRA_SUBJECT, fileName())
                putExtra(Intent.EXTRA_TEXT, json)
            }
            startActivity(Intent.createChooser(send, getString(R.string.share)))
        }
        binding.btnSave.setOnClickListener {
            if (dump == null) {
                Toast.makeText(this, R.string.no_dump, Toast.LENGTH_SHORT).show()
            } else {
                saveLauncher.launch(fileName())
            }
        }
        resetStatus()
    }

    override fun onResume() {
        super.onResume()
        nfcAdapter?.enableReaderMode(
            this,
            { tag -> analyzeAsync(tag) },
            NfcAdapter.FLAG_READER_NFC_F or NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK,
            null
        )
    }

    override fun onPause() {
        super.onPause()
        nfcAdapter?.disableReaderMode(this)
    }

    override fun onDestroy() {
        nfcExecutor.shutdownNow()
        super.onDestroy()
    }

    private fun analyzeAsync(tag: Tag) {
        if (!reading.compareAndSet(false, true)) return
        runOnUiThread { binding.status.setText(R.string.reading) }
        try {
            nfcExecutor.execute {
                try {
                    val result = runCatching { analyze(tag) }
                    runOnUiThread {
                        if (isFinishing || isDestroyed) return@runOnUiThread
                        result.onSuccess { d ->
                            dump = d
                            binding.btnShare.isEnabled = true
                            binding.btnSave.isEnabled = true
                            binding.status.text = d.error ?: "Done: ${d.log.size} commands"
                            render()
                        }.onFailure { e ->
                            binding.status.text = "Read failed: ${e.message}"
                        }
                    }
                } finally {
                    reading.set(false)
                }
            }
        } catch (e: Exception) {
            reading.set(false)
        }
    }

    private fun analyze(tag: Tag): CardDump {
        val nfc = NfcF.get(tag) ?: throw IllegalStateException("Not a FeliCa (NfcF) tag")
        nfc.connect()
        try {
            try {
                nfc.timeout = 1000
            } catch (_: Exception) {
            }
            val analyzer = CardAnalyzer(Transceiver { nfc.transceive(it) })
            return analyzer.analyze(tag.id, isoNow(), "SquirrelSuicaAnalysis android")
        } finally {
            try {
                nfc.close()
            } catch (_: Exception) {
            }
        }
    }

    private fun render() {
        val d = dump ?: return
        binding.output.text = if (showLog) DumpText.log(d) else DumpText.summary(d)
        binding.scroll.scrollTo(0, 0)
    }

    private fun currentJson(): String? = dump?.let { DumpJson.toJson(it, binding.redact.isChecked) }

    private fun fileName(): String {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        return "felica-dump-$stamp.json"
    }

    private fun isoNow(): String =
        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US)
            .apply { timeZone = TimeZone.getTimeZone("UTC") }
            .format(Date())

    private fun resetStatus() {
        binding.status.text = when {
            nfcAdapter == null -> getString(R.string.nfc_missing)
            nfcAdapter?.isEnabled != true -> getString(R.string.nfc_off)
            else -> getString(R.string.hold_card)
        }
    }
}
