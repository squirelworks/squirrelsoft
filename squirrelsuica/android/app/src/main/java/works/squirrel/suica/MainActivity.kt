package works.squirrel.suica

import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import works.squirrel.suica.databinding.ActivityMainBinding
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var nfcAdapter: NfcAdapter? = null

    /** All FeliCa I/O runs here; transceive blocks for up to the NFC timeout. */
    private val nfcExecutor: ExecutorService = Executors.newSingleThreadExecutor()
    private val reading = AtomicBoolean(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        nfcAdapter = NfcAdapter.getDefaultAdapter(this)
        resetStatus()

        binding.btnClear.setOnClickListener { clearUi() }

        handleIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        val adapter = nfcAdapter ?: return
        // Reader mode: FeliCa only, no NDEF probe (which could lose the tag before
        // our reads), callback off the UI thread.
        adapter.enableReaderMode(
            this,
            { tag -> readTagAsync(tag) },
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

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent == null) return
        val tag: Tag? = if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(NfcAdapter.EXTRA_TAG, Tag::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(NfcAdapter.EXTRA_TAG)
        }
        if (tag == null) return
        readTagAsync(tag)
    }

    /** Read [tag] on [nfcExecutor] and post the result to the UI; one read at a time. */
    private fun readTagAsync(tag: Tag) {
        if (!reading.compareAndSet(false, true)) return
        runOnUiThread { binding.status.setText(R.string.reading) }
        try {
            nfcExecutor.execute {
                try {
                    val result = runCatching { FelicaReader.read(tag) }
                    runOnUiThread {
                        if (isFinishing || isDestroyed) return@runOnUiThread
                        result.fold(::onCardRead, ::onReadFailed)
                    }
                } finally {
                    reading.set(false)
                }
            }
        } catch (e: Exception) {
            // Executor shut down (activity finishing).
            reading.set(false)
        }
    }

    private fun onCardRead(card: FelicaCard) {
        showCard(card)
        val msg = if (card.balanceYen != null) {
            "${card.brandLabel} ¥${card.balanceYen}"
        } else {
            card.brandLabel
        }
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }

    private fun onReadFailed(e: Throwable) {
        clearCardValues()
        val tip = if (e.message?.contains("lost", ignoreCase = true) == true) {
            "Tag was lost — hold the card still on the NFC spot and try again."
        } else {
            "Read failed: ${e.message}"
        }
        binding.status.text = tip
        binding.history.text = HistoryFormat.emptyMessage(
            HistoryStatus.READ_FAILED,
            tip
        )
        Toast.makeText(this, tip, Toast.LENGTH_LONG).show()
    }

    private fun showCard(card: FelicaCard) {
        binding.prompt.text = card.brandLabel
        binding.promptEn.text = card.note.ifBlank { getString(R.string.hold_card_en) }
        binding.balance.visibility = View.VISIBLE
        binding.idm.visibility = View.VISIBLE
        binding.balance.text = if (card.balanceYen != null) {
            getString(R.string.balance_fmt, card.balanceYen)
        } else {
            getString(R.string.balance_unknown)
        }
        binding.idm.text = getString(R.string.idm_fmt, card.idm) +
            "\nPMm ${card.pmm}\nSystem ${card.systemCode}"
        binding.status.text = card.note.ifBlank { "Live FeliCa read" }
        binding.history.text = HistoryFormat.formatHistoryPanel(card)
    }

    private fun clearCardValues() {
        binding.prompt.setText(R.string.hold_card)
        binding.promptEn.setText(R.string.hold_card_en)
        binding.balance.visibility = View.GONE
        binding.idm.visibility = View.GONE
        binding.history.text = getString(R.string.history_placeholder)
    }

    private fun clearUi() {
        clearCardValues()
        resetStatus()
    }

    private fun resetStatus() {
        binding.status.text = when {
            nfcAdapter == null -> getString(R.string.nfc_missing)
            nfcAdapter?.isEnabled != true -> getString(R.string.nfc_off)
            else -> getString(R.string.hold_card_en)
        }
    }
}
