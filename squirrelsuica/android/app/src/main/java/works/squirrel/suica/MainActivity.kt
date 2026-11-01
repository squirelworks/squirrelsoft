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
        when {
            nfcAdapter == null -> binding.status.setText(R.string.nfc_missing)
            nfcAdapter?.isEnabled != true -> binding.status.setText(R.string.nfc_off)
            else -> binding.status.text = getString(R.string.hold_card_en)
        }

        binding.btnDemo.setOnClickListener { showCard(SuicaParse.demoCard(), demo = true) }
        binding.btnClear.setOnClickListener { clearUi() }
        binding.btnQr.setOnClickListener {
            startActivity(Intent(this, QrScanActivity::class.java))
        }

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
            val card = SuicaReader.read(tag)
            showCard(card, demo = false)
            Toast.makeText(this, "Suica read OK", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            binding.status.text = "Read failed: ${e.message}"
            Toast.makeText(this, "Read failed: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showCard(card: SuicaCard, demo: Boolean) {
        binding.prompt.text = if (demo) "Demo Suica" else "Suica / PASMO"
        binding.promptEn.text = if (demo) "Synthetic card (no NFC)" else getString(R.string.hold_card_en)
        binding.balance.visibility = View.VISIBLE
        binding.idm.visibility = View.VISIBLE
        binding.balance.text = getString(R.string.balance_fmt, card.balanceYen)
        binding.idm.text = getString(R.string.idm_fmt, card.idm) +
            "\nPMm ${card.pmm}\nSystem ${card.systemCode}"
        binding.status.text = if (demo) "Demo payload loaded" else "Live FeliCa read"
        binding.history.text = if (card.history.isEmpty()) {
            "(no history blocks)"
        } else {
            card.history.mapIndexed { i, h ->
                "%2d. %s  %-6s  fare ¥%d  bal ¥%d  %s".format(
                    i + 1, h.date, h.kind, h.fareYen, h.balanceYen, h.note
                )
            }.joinToString("\n")
        }
    }

    private fun clearUi() {
        binding.prompt.setText(R.string.hold_card)
        binding.promptEn.setText(R.string.hold_card_en)
        binding.balance.visibility = View.GONE
        binding.idm.visibility = View.GONE
        binding.history.text = "(empty)"
        binding.status.text = when {
            nfcAdapter == null -> getString(R.string.nfc_missing)
            nfcAdapter?.isEnabled != true -> getString(R.string.nfc_off)
            else -> getString(R.string.hold_card_en)
        }
    }
}
