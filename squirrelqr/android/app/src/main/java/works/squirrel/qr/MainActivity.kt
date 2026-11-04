package works.squirrel.qr

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.RectF
import android.media.Image
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.core.UseCaseGroup
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import works.squirrel.qr.databinding.ActivityMainBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * SquirrelQR = QR / barcode camera reader only.
 * Spectrum lives in SquirrelSound; FeliCa lives in SquirrelSuica.
 */
class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var historyStore: ScanHistoryStore
    private val executor = Executors.newSingleThreadExecutor()
    private val lastPublished = AtomicReference("")
    private val lastOverlayMs = AtomicLong(0L)
    private val sqqrFrames = AtomicLong(0L)
    private var history: MutableList<ScanEntry> = mutableListOf()
    private var cameraBound = false
    @Volatile private var scanMode = ScanMode.DEFAULT

    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startCamera()
            else Toast.makeText(this, R.string.camera_permission, Toast.LENGTH_LONG).show()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        historyStore = ScanHistoryStore(this)
        history = historyStore.load()
        scanMode = ScanMode.fromId(
            getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_SCAN_MODE, null)
        )
        binding.overlay.mode = scanMode

        applySystemInsets()
        wireActions()
        renderLast(null)
        renderHistory()
    }

    private fun applySystemInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            binding.topBar.updatePadding(top = bars.top)
            // Keep history / buttons clear of the home indicator / nav gesture area.
            binding.resultPanel.updatePadding(bottom = bars.bottom + dp(8))
            insets
        }
        ViewCompat.requestApplyInsets(binding.root)
    }

    private fun wireActions() {
        binding.btnCopy.isEnabled = false
        binding.btnOpen.isEnabled = false

        renderModeToggle()
        binding.modeToggle.setOnClickListener {
            scanMode = if (scanMode == ScanMode.FIXED_GUIDE) ScanMode.TRACKING else ScanMode.FIXED_GUIDE
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(KEY_SCAN_MODE, scanMode.id).apply()
            binding.overlay.clear()
            binding.overlay.mode = scanMode
            binding.qrHint.setText(hintFor(scanMode))
            renderModeToggle()
        }

        binding.btnCopy.setOnClickListener {
            val text = lastPublished.get()
            if (text.isEmpty()) return@setOnClickListener
            val cm = getSystemService(ClipboardManager::class.java)
            cm.setPrimaryClip(ClipData.newPlainText("qr", text))
            Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
        }

        binding.btnOpen.setOnClickListener {
            val text = lastPublished.get()
            openIfUrl(text)
        }

        binding.btnClearHistory.setOnClickListener {
            history.clear()
            historyStore.clear()
            renderHistory()
            Toast.makeText(this, R.string.history_cleared, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onResume() {
        super.onResume()
        when {
            ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED -> startCamera()
            else -> permissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    override fun onPause() {
        stopCamera()
        super.onPause()
    }

    private fun startCamera() {
        if (cameraBound) return
        val providerFuture = ProcessCameraProvider.getInstance(this)
        providerFuture.addListener({
            if (isFinishing || isDestroyed) return@addListener
            val provider = providerFuture.get()
            // Same aspect ratio for preview and analysis so both see the same field of
            // view; the PreviewView then crops both identically (FILL_CENTER).
            val sameAspect = ResolutionSelector.Builder()
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                .build()
            val previewUseCase = Preview.Builder()
                .setResolutionSelector(sameAspect)
                .build().also { useCase ->
                    useCase.setSurfaceProvider(binding.preview.surfaceProvider)
                }
            val analysis = ImageAnalysis.Builder()
                .setResolutionSelector(sameAspect)
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()
            // Product scope: QR codes only (no 1D / Aztec / PDF417 / etc.).
            val options = BarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .build()
            val scanner = BarcodeScanning.getClient(options)
            analysis.setAnalyzer(executor) { proxy ->
                val media = proxy.image
                if (media == null) {
                    proxy.close()
                    return@setAnalyzer
                }
                val rotation = proxy.imageInfo.rotationDegrees
                val image = InputImage.fromMediaImage(media, rotation)
                // Unrotated buffer size; boxes come back upright (see PreviewMapper).
                val bufW = image.width
                val bufH = image.height
                scanner.process(image)
                    .addOnSuccessListener { codes ->
                        val hit = codes.firstOrNull { !it.rawValue.isNullOrBlank() }
                        if (hit != null) {
                            val box = hit.boundingBox?.let { intArrayOf(it.left, it.top, it.right, it.bottom) }
                            handleCode(hit.rawValue ?: return@addOnSuccessListener, box, bufW, bufH, rotation)
                            return@addOnSuccessListener
                        }
                        // ML Kit found nothing: every other such frame, try Squirrel's own
                        // decoder (libs/sqqr), which also reads light-on-dark codes. The
                        // luminance plane is copied here, while the frame is still open,
                        // and decoded off the main thread.
                        val gray = if (SqQr.available && sqqrFrames.incrementAndGet() % 2L == 0L) {
                            lumaCopy(media)
                        } else {
                            null
                        }
                        if (gray == null) {
                            maybeClearOverlay()
                            return@addOnSuccessListener
                        }
                        val task = Runnable {
                            val found = SqQr.decode(gray, bufW, bufH)
                            runOnUiThread {
                                if (found == null) {
                                    maybeClearOverlay()
                                } else {
                                    val box = PreviewMapper.uprightBounds(found.corners, bufW, bufH, rotation)
                                    handleCode(found.text, box, bufW, bufH, rotation)
                                }
                            }
                        }
                        try {
                            executor.execute(task)
                        } catch (_: RejectedExecutionException) {
                            // The activity is being destroyed.
                        }
                    }
                    .addOnCompleteListener { proxy.close() }
            }
            try {
                provider.unbindAll()
                val group = UseCaseGroup.Builder()
                    .addUseCase(previewUseCase)
                    .addUseCase(analysis)
                    .build()
                provider.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, group)
                cameraBound = true
                binding.qrHint.setText(hintFor(scanMode))
            } catch (e: Exception) {
                Toast.makeText(this, "Camera bind failed: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }, ContextCompat.getMainExecutor(this))
    }

    /**
     * A decoded code with its upright box (left, top, right, bottom in the rotated
     * frame, or null). Runs on the main thread.
     */
    private fun handleCode(text: String, box: IntArray?, bufW: Int, bufH: Int, rotation: Int) {
        if (text.isBlank()) return
        val viewW = binding.overlay.width
        val viewH = binding.overlay.height
        val mapped = box?.let {
            PreviewMapper.mapBoxForBuffer(it[0], it[1], it[2], it[3], bufW, bufH, rotation, viewW, viewH)
        }
        lastOverlayMs.set(System.currentTimeMillis())
        if (scanMode == ScanMode.FIXED_GUIDE) {
            val guide = ScanGuide.guideBox(viewW, viewH)
            val inside = mapped != null && guide != null && ScanGuide.isInside(mapped, guide)
            binding.overlay.guideHit = inside
            if (!inside) {
                binding.qrHint.setText(R.string.qr_move_into_frame)
                return
            }
        } else if (mapped != null) {
            binding.overlay.setTarget(RectF(mapped.left, mapped.top, mapped.right, mapped.bottom))
        }
        if (text == lastPublished.get()) return
        lastPublished.set(text)
        publishScan(ScanEntry(text, "QR", System.currentTimeMillis()))
    }

    /** The Y (luminance) plane as a tightly packed width×height array. */
    private fun lumaCopy(media: Image): ByteArray? {
        val plane = media.planes.firstOrNull() ?: return null
        if (plane.pixelStride != 1) return null
        val w = media.width
        val h = media.height
        val out = ByteArray(w * h)
        val buf = plane.buffer.duplicate()
        val stride = plane.rowStride
        for (y in 0 until h) {
            buf.position(y * stride)
            buf.get(out, y * w, w)
        }
        return out
    }

    private fun maybeClearOverlay() {
        val age = System.currentTimeMillis() - lastOverlayMs.get()
        if (age > 450L) {
            runOnUiThread { binding.overlay.clear() }
        }
    }

    private fun hintFor(mode: ScanMode): Int =
        if (mode == ScanMode.FIXED_GUIDE) R.string.qr_hint_fixed else R.string.qr_hint

    private fun renderModeToggle() {
        binding.modeToggle.setText(
            if (scanMode == ScanMode.FIXED_GUIDE) R.string.mode_fixed else R.string.mode_tracking
        )
    }

    private fun publishScan(entry: ScanEntry) {
        history.removeAll { it.text == entry.text }
        history.add(0, entry)
        while (history.size > ScanHistoryStore.MAX) {
            history.removeAt(history.lastIndex)
        }
        historyStore.save(history)
        renderLast(entry)
        renderHistory()
        binding.qrHint.setText(R.string.qr_found)
    }

    private fun renderLast(entry: ScanEntry?) {
        if (entry == null) {
            val latest = history.firstOrNull()
            if (latest != null) {
                lastPublished.set(latest.text)
                binding.qrResult.text = "[${latest.format}] ${latest.text}"
                binding.btnCopy.isEnabled = true
                binding.btnOpen.isEnabled = QrContent.isHttpUrl(latest.text)
            } else {
                binding.qrResult.setText(R.string.qr_empty)
                binding.btnCopy.isEnabled = false
                binding.btnOpen.isEnabled = false
            }
            return
        }
        binding.qrResult.text = "[${entry.format}] ${entry.text}"
        binding.btnCopy.isEnabled = true
        binding.btnOpen.isEnabled = QrContent.isHttpUrl(entry.text)
    }

    private fun renderHistory() {
        val list = binding.historyList
        list.removeAllViews()
        if (history.isEmpty()) {
            val empty = TextView(this).apply {
                setText(R.string.history_empty)
                setTextColor(ContextCompat.getColor(this@MainActivity, R.color.muted))
                textSize = 13f
                setPadding(0, dp(8), 0, dp(8))
            }
            list.addView(empty)
            return
        }
        val inflater = LayoutInflater.from(this)
        for (entry in history) {
            val row = inflater.inflate(R.layout.item_history_row, list, false)
            row.findViewById<TextView>(R.id.rowMeta).text =
                "${entry.format} · ${timeFmt.format(Date(entry.timestampMs))}"
            row.findViewById<TextView>(R.id.rowText).text =
                QrContent.displayLabel(entry.text)
            val action = row.findViewById<TextView>(R.id.rowAction)
            if (QrContent.isHttpUrl(entry.text)) {
                action.visibility = View.VISIBLE
                action.setText(R.string.link_badge)
            } else {
                action.visibility = View.GONE
            }
            row.setOnClickListener {
                lastPublished.set(entry.text)
                renderLast(entry)
                if (QrContent.isHttpUrl(entry.text)) {
                    openIfUrl(entry.text)
                } else {
                    val cm = getSystemService(ClipboardManager::class.java)
                    cm.setPrimaryClip(ClipData.newPlainText("qr", entry.text))
                    Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
                }
            }
            list.addView(row)
        }
    }

    private fun openIfUrl(text: String) {
        if (!QrContent.isHttpUrl(text)) return
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(text.trim())))
        } catch (_: Exception) {
            Toast.makeText(this, R.string.open_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopCamera() {
        cameraBound = false
        try {
            ProcessCameraProvider.getInstance(this).get().unbindAll()
        } catch (_: Exception) {
        }
        binding.overlay.clear()
    }

    override fun onDestroy() {
        executor.shutdown()
        super.onDestroy()
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    companion object {
        private const val PREFS = "squirrelqr"
        private const val KEY_SCAN_MODE = "scan_mode"
    }
}
