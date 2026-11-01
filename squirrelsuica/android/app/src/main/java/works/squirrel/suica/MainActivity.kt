package works.squirrel.suica

import android.app.Activity
import android.app.PendingIntent
import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var out: TextView
    private var adapter: NfcAdapter? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        out = TextView(this).apply {
            textSize = 16f
            setPadding(48, 48, 48, 48)
            text = "SquirrelSuica\nHold a Suica/PASMO to the NFC area."
        }
        setContentView(ScrollView(this).apply { addView(out) })
        adapter = NfcAdapter.getDefaultAdapter(this)
        if (adapter == null) out.text = "NFC not available on this device."
        handleIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        val pi = PendingIntent.getActivity(
            this, 0,
            Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_MUTABLE
        )
        adapter?.enableForegroundDispatch(
            this, pi, null,
            arrayOf(arrayOf("android.nfc.tech.NfcF"))
        )
    }

    override fun onPause() {
        super.onPause()
        adapter?.disableForegroundDispatch(this)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        @Suppress("DEPRECATION")
        val tag: Tag = intent?.getParcelableExtra(NfcAdapter.EXTRA_TAG) ?: return
        try {
            val r = SuicaReader.read(tag)
            out.text = "IDm ${r.idmHex}\nBalance ¥${r.balanceYen}\nRaw ${r.rawBalanceHex}"
        } catch (e: Exception) {
            out.text = "Read failed: ${e.message}"
        }
    }
}
