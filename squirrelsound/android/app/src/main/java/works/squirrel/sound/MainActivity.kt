package works.squirrel.sound

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import com.google.android.material.tabs.TabLayout
import works.squirrel.sound.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var engine: AudioEngine? = null
    private var maxHold = false
    /** User intends live capture (auto-start / Start). Cleared only by Stop. */
    private var wantLive = true
    private var awaitingPermission = false
    /** Prevents overlapping startEngine from permission + onResume races. */
    private var starting = false
    private var activePreset: SpectrumControls.RangePreset = SpectrumControls.RangePreset.HUMAN
    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }

    private val micPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
            awaitingPermission = false
            if (ok) {
                if (wantLive && !isFinishing && !isDestroyed) {
                    startEngine(AudioEngine.Source.MIC)
                }
            } else {
                wantLive = false
                safeUi {
                    binding.status.setTextColor(ContextCompat.getColor(this, R.color.warm))
                    binding.status.setText(R.string.status_mic_denied)
                }
                Toast.makeText(
                    applicationContext,
                    R.string.mic_permission,
                    Toast.LENGTH_LONG
                ).show()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupTabs()
        setupBasicControls()
        setupAdvancedControls()

        binding.spectrum.applyPreset(SpectrumControls.RangePreset.HUMAN)
        syncAdvancedFieldsFromView()

        binding.btnStop.isEnabled = false
        // Spectroid-style: start the mic as soon as the app opens.
        binding.root.post { if (wantLive && !isFinishing) requestStart() }
    }

    private fun setupTabs() {
        binding.tabs.addTab(binding.tabs.newTab().setText(R.string.tab_basic))
        binding.tabs.addTab(binding.tabs.newTab().setText(R.string.tab_advanced))
        binding.tabs.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab) {
                val advanced = tab.position == 1
                binding.panelBasic.visibility = if (advanced) android.view.View.GONE else android.view.View.VISIBLE
                binding.panelAdvanced.visibility = if (advanced) android.view.View.VISIBLE else android.view.View.GONE
                if (advanced) syncAdvancedFieldsFromView()
            }

            override fun onTabUnselected(tab: TabLayout.Tab) {}
            override fun onTabReselected(tab: TabLayout.Tab) {}
        })
        binding.tabs.getTabAt(0)?.select()
    }

    private fun setupBasicControls() {
        binding.btnStart.setOnClickListener {
            wantLive = true
            requestStart()
        }
        binding.btnStop.setOnClickListener {
            wantLive = false
            stopEngine(showIdle = true)
        }
        binding.btnHold.setOnClickListener {
            maxHold = !maxHold
            binding.spectrum.setMaxHold(maxHold)
            binding.btnHold.text = getString(if (maxHold) R.string.hold_on else R.string.hold)
        }
        binding.btnReset.setOnClickListener {
            binding.spectrum.resetHold()
            binding.spectrum.clearWaterfall()
            applyPreset(activePreset, toast = false)
            binding.spectrum.invalidate()
        }

        binding.btnZoomIn.setOnClickListener {
            binding.spectrum.zoomBy(1.35f)
            syncAdvancedFieldsFromView()
        }
        binding.btnZoomOut.setOnClickListener {
            binding.spectrum.zoomBy(1f / 1.35f)
            syncAdvancedFieldsFromView()
        }

        binding.rangePresets.setOnCheckedStateChangeListener { _, checkedIds ->
            if (checkedIds.isEmpty()) return@setOnCheckedStateChangeListener
            val preset = when (checkedIds[0]) {
                R.id.chipMusic -> SpectrumControls.RangePreset.MUSIC
                R.id.chipBass -> SpectrumControls.RangePreset.BASS
                R.id.chipSpeech -> SpectrumControls.RangePreset.SPEECH
                R.id.chipFull -> SpectrumControls.RangePreset.FULL
                else -> SpectrumControls.RangePreset.HUMAN
            }
            applyPreset(preset, toast = true)
        }

        val spanLabels = listOf(
            getString(R.string.span_5s),
            getString(R.string.span_15s),
            getString(R.string.span_30s),
            getString(R.string.span_60s),
            getString(R.string.span_2min),
            getString(R.string.span_5min)
        )
        binding.scrollSpeed.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, spanLabels)
        binding.scrollSpeed.setSelection(
            SpectrumControls.WaterfallDuration.values()
                .indexOf(SpectrumControls.WaterfallDuration.DEFAULT)
        )
        binding.scrollSpeed.onItemSelectedListener = simpleItemSelected {
            applyWaterfallDuration(selectedWaterfallDuration())
        }

        // Waterfall / Fountain: applies live (also to rows already drawn) and is
        // remembered between sessions.
        val directions = SpectrumControls.WaterfallDirection.values()
        val directionLabels = directions.map {
            getString(
                when (it) {
                    SpectrumControls.WaterfallDirection.WATERFALL -> R.string.direction_waterfall
                    SpectrumControls.WaterfallDirection.FOUNTAIN -> R.string.direction_fountain
                }
            )
        }
        binding.waterfallDirection.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, directionLabels)
        val savedDirection = SpectrumControls.WaterfallDirection.fromId(
            prefs.getString(KEY_DIRECTION, null)
        )
        binding.spectrum.direction = savedDirection
        binding.waterfallDirection.setSelection(directions.indexOf(savedDirection))
        binding.waterfallDirection.onItemSelectedListener = simpleItemSelected {
            val d = directions.getOrNull(binding.waterfallDirection.selectedItemPosition)
                ?: SpectrumControls.WaterfallDirection.DEFAULT
            binding.spectrum.direction = d
            prefs.edit().putString(KEY_DIRECTION, d.id).apply()
        }
    }

    private fun setupAdvancedControls() {
        val sizes = listOf("1024", "2048", "4096", "8192")
        binding.fftSize.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, sizes)
        binding.fftSize.setSelection(1)

        val smoothLabels = listOf(
            getString(R.string.smooth_off),
            getString(R.string.smooth_light),
            getString(R.string.smooth_medium),
            getString(R.string.smooth_heavy)
        )
        binding.smoothAmount.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, smoothLabels)
        binding.smoothAmount.setSelection(2) // Medium ≈ 0.30
        binding.smoothAmount.onItemSelectedListener = simpleItemSelected {
            engine?.smooth = selectedSmooth()
        }

        binding.btnApplyRange.setOnClickListener { applyCustomRangeFromFields() }

        setupLevelControls()
        setupMicControls()
        setupSplitControl()
    }

    /**
     * Display range (dBFS): two sliders over FLOOR_DB..0 plus Auto, which follows
     * the noise floor and recent peaks. Moving a slider turns Auto off. All three
     * are remembered between sessions.
     */
    private fun setupLevelControls() {
        val floor = Levels.FLOOR_DB.toInt()
        fun dbOf(progress: Int) = (floor + progress).toFloat()
        fun progressOf(db: Float) = (db.toInt() - floor).coerceIn(0, -floor)

        binding.spectrum.setDbRange(
            prefs.getFloat(KEY_LEVEL_MIN, Levels.DEFAULT_MIN_DB),
            prefs.getFloat(KEY_LEVEL_MAX, Levels.DEFAULT_MAX_DB)
        )
        binding.spectrum.autoRange = prefs.getBoolean(KEY_AUTO_RANGE, false)
        binding.autoRange.isChecked = binding.spectrum.autoRange
        binding.levelMin.progress = progressOf(binding.spectrum.dbMin)
        binding.levelMax.progress = progressOf(binding.spectrum.dbMax)
        updateLevelLabel()

        val listener = object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: android.widget.SeekBar?, p: Int, fromUser: Boolean) {
                if (!fromUser) return
                if (binding.autoRange.isChecked) binding.autoRange.isChecked = false
                binding.spectrum.setDbRange(
                    dbOf(binding.levelMin.progress),
                    dbOf(binding.levelMax.progress)
                )
                updateLevelLabel()
            }

            override fun onStartTrackingTouch(bar: android.widget.SeekBar?) {}

            override fun onStopTrackingTouch(bar: android.widget.SeekBar?) {
                // Show the clamped range (min span) on the sliders and save it.
                binding.levelMin.progress = progressOf(binding.spectrum.dbMin)
                binding.levelMax.progress = progressOf(binding.spectrum.dbMax)
                prefs.edit()
                    .putFloat(KEY_LEVEL_MIN, binding.spectrum.dbMin)
                    .putFloat(KEY_LEVEL_MAX, binding.spectrum.dbMax)
                    .apply()
            }
        }
        binding.levelMin.setOnSeekBarChangeListener(listener)
        binding.levelMax.setOnSeekBarChangeListener(listener)

        binding.autoRange.setOnCheckedChangeListener { _, on ->
            binding.spectrum.autoRange = on
            if (!on) {
                // Back to the manual range on the sliders.
                binding.spectrum.setDbRange(
                    dbOf(binding.levelMin.progress),
                    dbOf(binding.levelMax.progress)
                )
            }
            updateLevelLabel()
            prefs.edit().putBoolean(KEY_AUTO_RANGE, on).apply()
        }
    }

    /**
     * Mic source (restarts live mic capture so it takes effect) and digital gain
     * 0..30 dB (applies live). Both are remembered between sessions.
     */
    private fun setupMicControls() {
        val sources = MicSettings.Source.PICKABLE
        val labels = sources.map {
            getString(
                when (it) {
                    MicSettings.Source.UNPROCESSED -> R.string.mic_source_unprocessed
                    MicSettings.Source.CAMCORDER -> R.string.mic_source_camcorder
                    MicSettings.Source.VOICE_RECOGNITION -> R.string.mic_source_voice
                    else -> R.string.mic_source_mic
                }
            )
        }
        binding.micSource.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        val saved = MicSettings.Source.fromId(prefs.getString(KEY_MIC_SOURCE, null))
        binding.micSource.setSelection(sources.indexOf(saved))
        binding.micSource.onItemSelectedListener = simpleItemSelected {
            val chosen = selectedMicSource()
            val current = prefs.getString(KEY_MIC_SOURCE, MicSettings.Source.PREFERRED.id)
            if (current == chosen.id) return@simpleItemSelected
            prefs.edit().putString(KEY_MIC_SOURCE, chosen.id).apply()
            val eng = engine
            if (eng != null && eng.source == AudioEngine.Source.MIC) {
                startEngine(AudioEngine.Source.MIC)
            }
        }

        binding.gain.progress = prefs.getInt(KEY_GAIN_DB, 0).coerceIn(0, MicSettings.MAX_GAIN_DB.toInt())
        updateGainLabel()
        binding.gain.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: android.widget.SeekBar?, p: Int, fromUser: Boolean) {
                engine?.gainDb = p.toFloat()
                updateGainLabel()
            }

            override fun onStartTrackingTouch(bar: android.widget.SeekBar?) {}

            override fun onStopTrackingTouch(bar: android.widget.SeekBar?) {
                prefs.edit().putInt(KEY_GAIN_DB, binding.gain.progress).apply()
            }
        })
    }

    private fun selectedMicSource(): MicSettings.Source =
        MicSettings.Source.PICKABLE.getOrNull(binding.micSource.selectedItemPosition)
            ?: MicSettings.Source.PREFERRED

    private fun updateGainLabel() {
        binding.gainLabel.text = getString(R.string.gain_label, binding.gain.progress)
     * Spectrum / LOFARgram split (50/50 default). The LOFARgram has one row per
     * pixel of its half, so the engine is told the row count whenever it changes.
     */
    private fun setupSplitControl() {
        binding.spectrum.onWaterfallRowsChanged = { rows -> engine?.waterfallRows = rows }
        val ratios = SpectrumControls.SplitRatio.values()
        val labels = ratios.map {
            getString(
                when (it) {
                    SpectrumControls.SplitRatio.SPECTRUM_SMALL -> R.string.split_40_60
                    SpectrumControls.SplitRatio.SPECTRUM_LARGE -> R.string.split_60_40
                    SpectrumControls.SplitRatio.EVEN -> R.string.split_50_50
                }
            )
        }
        binding.splitRatio.adapter =
            ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        val saved = SpectrumControls.SplitRatio.fromId(prefs.getString(KEY_SPLIT, null))
        binding.spectrum.splitRatio = saved
        binding.splitRatio.setSelection(ratios.indexOf(saved))
        binding.splitRatio.onItemSelectedListener = simpleItemSelected {
            val r = ratios.getOrNull(binding.splitRatio.selectedItemPosition)
                ?: SpectrumControls.SplitRatio.DEFAULT
            binding.spectrum.splitRatio = r
            prefs.edit().putString(KEY_SPLIT, r.id).apply()
        }
    }

    private fun updateLevelLabel() {
        binding.levelRangeLabel.text = getString(
            R.string.level_range,
            binding.spectrum.dbMin.toInt(),
            binding.spectrum.dbMax.toInt()
        )
    }

    private fun applyPreset(preset: SpectrumControls.RangePreset, toast: Boolean) {
        activePreset = preset
        binding.spectrum.applyPreset(preset)
        syncAdvancedFieldsFromView()
        if (toast) {
            Toast.makeText(
                applicationContext,
                getString(
                    R.string.range_applied,
                    binding.spectrum.fMin,
                    binding.spectrum.fMax
                ),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun applyCustomRangeFromFields() {
        val lo = binding.editFmin.text?.toString()?.toFloatOrNull()
        val hi = binding.editFmax.text?.toString()?.toFloatOrNull()
        if (lo == null || hi == null || hi <= lo) {
            Toast.makeText(applicationContext, R.string.range_invalid, Toast.LENGTH_SHORT).show()
            return
        }
        binding.spectrum.setFrequencyRange(lo, hi)
        syncAdvancedFieldsFromView()
        Toast.makeText(
            applicationContext,
            getString(R.string.range_applied, binding.spectrum.fMin, binding.spectrum.fMax),
            Toast.LENGTH_SHORT
        ).show()
    }

    private fun syncAdvancedFieldsFromView() {
        if (!::binding.isInitialized) return
        binding.editFmin.setText(binding.spectrum.fMin.toInt().toString())
        binding.editFmax.setText(binding.spectrum.fMax.toInt().toString())
    }

    private fun selectedWaterfallDuration(): SpectrumControls.WaterfallDuration =
        SpectrumControls.WaterfallDuration.values()
            .getOrNull(binding.scrollSpeed.selectedItemPosition)
            ?: SpectrumControls.WaterfallDuration.DEFAULT

    private fun selectedSmooth(): Float =
        when (binding.smoothAmount.selectedItemPosition) {
            0 -> 0f
            1 -> 0.15f
            3 -> 0.55f
            else -> 0.30f
        }

    private fun applyWaterfallDuration(span: SpectrumControls.WaterfallDuration) {
        engine?.waterfallSeconds = span.seconds
        binding.spectrum.waterfallSeconds = span.seconds
    }

    private fun uiAlive(): Boolean =
        ::binding.isInitialized &&
            !isDestroyed &&
            !isFinishing &&
            lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)

    private inline fun safeUi(block: () -> Unit) {
        if (!uiAlive()) return
        try {
            block()
        } catch (_: Exception) {
            // View hierarchy may be tearing down; never crash from a status paint.
        }
    }

    private fun requestStart() {
        if (isFinishing || isDestroyed) return
        if (binding.demoTone.isChecked) {
            startEngine(AudioEngine.Source.DEMO)
            return
        }
        when {
            ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
                PackageManager.PERMISSION_GRANTED -> startEngine(AudioEngine.Source.MIC)
            else -> {
                awaitingPermission = true
                safeUi {
                    binding.status.setTextColor(ContextCompat.getColor(this, R.color.warm))
                    binding.status.setText(R.string.status_need_mic)
                }
                try {
                    micPermission.launch(Manifest.permission.RECORD_AUDIO)
                } catch (e: Exception) {
                    awaitingPermission = false
                    wantLive = false
                    safeUi {
                        binding.status.setText(R.string.status_mic_denied)
                    }
                    Toast.makeText(
                        applicationContext,
                        getString(R.string.status_perm_error, e.message ?: "error"),
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }
    }

    private fun startEngine(source: AudioEngine.Source) {
        if (isFinishing || isDestroyed) return
        if (starting) return
        starting = true
        try {
            stopEngine(showIdle = false)
            val fft = try {
                binding.fftSize.selectedItem?.toString()?.toIntOrNull() ?: 2048
            } catch (_: Exception) {
                2048
            }
            val span = selectedWaterfallDuration()
            val smooth = selectedSmooth()
            val eng = AudioEngine(
                appContext = applicationContext,
                onFrame = { frame ->
                    safeUi {
                        binding.spectrum.update(frame)
                        if (binding.spectrum.autoRange &&
                            binding.panelAdvanced.visibility == android.view.View.VISIBLE
                        ) {
                            updateLevelLabel()
                        }
                        val ok = !frame.sourceLabel.startsWith("mic-failed") &&
                            !frame.sourceLabel.startsWith("mic-denied") &&
                            frame.sourceLabel != "error"
                        binding.status.setTextColor(
                            ContextCompat.getColor(this, if (ok) R.color.ok else R.color.warm)
                        )
                        binding.status.text = getString(
                            R.string.status_live,
                            frame.sourceLabel,
                            frame.sampleRate,
                            frame.fftSize,
                            frame.peakHz,
                            frame.peakDb,
                            frame.rmsDb
                        )
                    }
                },
                onStatus = { msg ->
                    safeUi {
                        binding.status.setTextColor(ContextCompat.getColor(this, R.color.warm))
                        binding.status.text = msg
                    }
                    if (msg.contains("denied", ignoreCase = true) ||
                        msg.contains("unavailable", ignoreCase = true) ||
                        msg.contains("crashed", ignoreCase = true)
                    ) {
                        Toast.makeText(applicationContext, msg, Toast.LENGTH_SHORT).show()
                    }
                }
            )
            eng.fftSize = fft
            eng.waterfallSeconds = span.seconds
            eng.waterfallRows = binding.spectrum.waterfallRows
            binding.spectrum.waterfallSeconds = span.seconds
            eng.smooth = smooth
            eng.micSource = selectedMicSource()
            eng.gainDb = binding.gain.progress.toFloat()
            eng.source = source
            try {
                eng.start()
            } catch (e: Exception) {
                safeUi {
                    binding.status.setTextColor(ContextCompat.getColor(this, R.color.warm))
                    binding.status.text = getString(
                        R.string.status_start_failed,
                        e.message ?: e.javaClass.simpleName
                    )
                }
                Toast.makeText(
                    applicationContext,
                    getString(R.string.status_start_failed, e.message ?: "error"),
                    Toast.LENGTH_LONG
                ).show()
                wantLive = source == AudioEngine.Source.DEMO
                return
            }
            engine = eng
            safeUi {
                binding.btnStart.isEnabled = false
                binding.btnStop.isEnabled = true
                binding.fftSize.isEnabled = false
                binding.demoTone.isEnabled = false
                binding.status.setTextColor(ContextCompat.getColor(this, R.color.ok))
                binding.status.text = getString(
                    if (source == AudioEngine.Source.DEMO) R.string.status_starting_demo
                    else R.string.status_starting_mic
                )
            }
        } finally {
            starting = false
        }
    }

    private fun stopEngine(showIdle: Boolean) {
        val eng = engine
        engine = null
        try {
            eng?.stop()
        } catch (_: Exception) {
        }
        safeUi {
            binding.btnStart.isEnabled = true
            binding.btnStop.isEnabled = false
            binding.fftSize.isEnabled = true
            binding.demoTone.isEnabled = true
            if (showIdle) {
                binding.status.setTextColor(ContextCompat.getColor(this, R.color.ok))
                binding.status.setText(R.string.status_idle)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // Keep / restart capture while the spectrum screen is visible.
        if (wantLive && engine == null && !isFinishing && !awaitingPermission && !starting) {
            binding.root.post {
                if (wantLive && engine == null && !isFinishing && !awaitingPermission) {
                    requestStart()
                }
            }
        }
    }

    override fun onPause() {
        // Do NOT stop the mic here — notification shade / dialogs pause the
        // activity without leaving the spectrum screen. Spectroid-like apps
        // keep capturing while still visible.
        super.onPause()
    }

    override fun onStop() {
        super.onStop()
        // Leaving the app (home / another activity): release mic + wake lock.
        // Remember wantLive so onResume restarts when we come back.
        if (!awaitingPermission) {
            stopEngine(showIdle = false)
        }
    }

    override fun onDestroy() {
        wantLive = false
        awaitingPermission = false
        try {
            engine?.stop()
        } catch (_: Exception) {
        }
        engine = null
        super.onDestroy()
    }

    private companion object {
        const val PREFS = "squirrelsound"
        const val KEY_DIRECTION = "waterfall_direction"
        const val KEY_LEVEL_MIN = "level_min_db"
        const val KEY_LEVEL_MAX = "level_max_db"
        const val KEY_AUTO_RANGE = "auto_range"
        const val KEY_MIC_SOURCE = "mic_source"
        const val KEY_GAIN_DB = "gain_db"
        const val KEY_SPLIT = "split_ratio"
    }

    private fun simpleItemSelected(onSelected: () -> Unit) =
        object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(
                parent: android.widget.AdapterView<*>?,
                view: android.view.View?,
                position: Int,
                id: Long
            ) {
                onSelected()
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        }
}
