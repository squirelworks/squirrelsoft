package works.squirrel.sound

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import works.squirrel.sound.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var engine: AudioEngine? = null
    private var maxHold = false

    private val micPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
            if (ok) startEngine(AudioEngine.Source.MIC)
            else Toast.makeText(this, R.string.mic_permission, Toast.LENGTH_LONG).show()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val sizes = listOf("1024", "2048", "4096", "8192")
        binding.fftSize.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, sizes)
        binding.fftSize.setSelection(1)

        binding.btnStart.setOnClickListener { requestStart() }
        binding.btnStop.setOnClickListener { stopEngine() }
        binding.btnHold.setOnClickListener {
            maxHold = !maxHold
            binding.spectrum.setMaxHold(maxHold)
            binding.btnHold.text = getString(if (maxHold) R.string.hold_on else R.string.hold)
        }
        binding.btnReset.setOnClickListener {
            binding.spectrum.resetHold()
            binding.spectrum.fMin = 20f
            binding.spectrum.fMax = 8000f
            binding.spectrum.invalidate()
        }

        binding.btnStop.isEnabled = false

        // Spectroid-style: start the mic as soon as the app opens.
        binding.root.post { requestStart() }
    }

    private fun requestStart() {
        if (binding.demoTone.isChecked) {
            startEngine(AudioEngine.Source.DEMO)
            return
        }
        when {
            ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
                PackageManager.PERMISSION_GRANTED -> startEngine(AudioEngine.Source.MIC)
            else -> micPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    private fun startEngine(source: AudioEngine.Source) {
        stopEngine()
        val fft = binding.fftSize.selectedItem.toString().toInt()
        val eng = AudioEngine(
            onFrame = { db, sr, peak ->
                binding.spectrum.update(db, sr, peak)
                binding.status.text = getString(
                    R.string.status_live,
                    engine?.activeLabel ?: if (source == AudioEngine.Source.DEMO) "demo" else "mic",
                    sr,
                    fft,
                    peak
                )
            },
            onInfo = { msg ->
                Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
            }
        )
        eng.fftSize = fft
        eng.source = source
        eng.start()
        engine = eng
        binding.btnStart.isEnabled = false
        binding.btnStop.isEnabled = true
        binding.fftSize.isEnabled = false
        binding.demoTone.isEnabled = false
    }

    private fun stopEngine() {
        engine?.stop()
        engine = null
        binding.btnStart.isEnabled = true
        binding.btnStop.isEnabled = false
        binding.fftSize.isEnabled = true
        binding.demoTone.isEnabled = true
        binding.status.setText(R.string.status_idle)
    }

    override fun onResume() {
        super.onResume()
        if (engine == null && !isFinishing) {
            binding.root.post { requestStart() }
        }
    }

    override fun onPause() {
        super.onPause()
        stopEngine()
    }
}
