package works.squirrel.qr

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import works.squirrel.qr.databinding.ActivityMainBinding
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicReference

/** SquirrelQR: camera preview + barcode decode + copy/clear. */
class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private val executor = Executors.newSingleThreadExecutor()
    private val lastPublished = AtomicReference("")
    private var cameraBound = false

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startCamera()
            else Toast.makeText(this, R.string.camera_permission, Toast.LENGTH_LONG).show()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnCopy.isEnabled = false
        binding.btnCopy.setOnClickListener {
            val text = lastPublished.get()
            if (text.isEmpty()) return@setOnClickListener
            val cm = getSystemService(ClipboardManager::class.java)
            cm.setPrimaryClip(ClipData.newPlainText("qr", text))
            Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
        }
        binding.btnRescan.setOnClickListener {
            lastPublished.set("")
            binding.qrResult.text = ""
            binding.qrHint.setText(R.string.qr_hint)
            binding.btnCopy.isEnabled = false
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
            val previewUseCase = Preview.Builder().build().also { useCase ->
                useCase.setSurfaceProvider(binding.preview.surfaceProvider)
            }
            val analysis = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()
            val options = BarcodeScannerOptions.Builder()
                .setBarcodeFormats(
                    Barcode.FORMAT_QR_CODE,
                    Barcode.FORMAT_AZTEC,
                    Barcode.FORMAT_DATA_MATRIX,
                    Barcode.FORMAT_PDF417,
                    Barcode.FORMAT_CODE_128,
                    Barcode.FORMAT_EAN_13
                )
                .build()
            val scanner = BarcodeScanning.getClient(options)
            analysis.setAnalyzer(executor) { proxy ->
                val media = proxy.image
                if (media == null) {
                    proxy.close()
                    return@setAnalyzer
                }
                val image = InputImage.fromMediaImage(media, proxy.imageInfo.rotationDegrees)
                scanner.process(image)
                    .addOnSuccessListener { codes ->
                        val hit = codes.firstOrNull { !it.rawValue.isNullOrBlank() } ?: return@addOnSuccessListener
                        val text = hit.rawValue ?: return@addOnSuccessListener
                        val prev = lastPublished.get()
                        if (text == prev) return@addOnSuccessListener
                        lastPublished.set(text)
                        val fmt = formatName(hit.format)
                        runOnUiThread {
                            binding.qrResult.text = "[$fmt] $text"
                            binding.btnCopy.isEnabled = true
                            binding.qrHint.setText(R.string.qr_found)
                        }
                    }
                    .addOnCompleteListener { proxy.close() }
            }
            try {
                provider.unbindAll()
                provider.bindToLifecycle(
                    this, CameraSelector.DEFAULT_BACK_CAMERA, previewUseCase, analysis
                )
                cameraBound = true
                binding.qrHint.setText(R.string.qr_hint)
            } catch (e: Exception) {
                Toast.makeText(this, "Camera bind failed: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun stopCamera() {
        cameraBound = false
        try {
            ProcessCameraProvider.getInstance(this).get().unbindAll()
        } catch (_: Exception) {
        }
    }

    override fun onDestroy() {
        executor.shutdown()
        super.onDestroy()
    }

    private fun formatName(format: Int): String = when (format) {
        Barcode.FORMAT_QR_CODE -> "QR"
        Barcode.FORMAT_AZTEC -> "Aztec"
        Barcode.FORMAT_DATA_MATRIX -> "DataMatrix"
        Barcode.FORMAT_PDF417 -> "PDF417"
        Barcode.FORMAT_CODE_128 -> "Code128"
        Barcode.FORMAT_EAN_13 -> "EAN-13"
        else -> "code"
    }
}
