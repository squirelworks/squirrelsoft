package works.squirrel.suica

import android.app.PendingIntent
import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import works.squirrel.suica.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var nfcAdapter: NfcAdapter? = null

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
        val intent = Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        val flags = if (Build.VERSION.SDK_INT >= 31) {
            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        } else {
            PendingIntent.FLAG_UPDATE_CURRENT
        }
        val pi = PendingIntent.getActivity(this, 0, intent, flags)
        adapter.enableForegroundDispatch(
            this,
            pi,
            null,
            arrayOf(arrayOf(android.nfc.tech.NfcF::class.java.name))
        )
    }

    override fun onPause() {
        super.onPause()
        nfcAdapter?.disableForegroundDispatch(this)
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
        try {
            val card = FelicaReader.read(tag)
            showCard(card)
            val msg = if (card.balanceYen != null) {
                "${card.brandLabel} ¥${card.balanceYen}"
            } else {
                card.brandLabel
            }
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
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
