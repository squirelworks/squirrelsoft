package works.squirrel.field

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
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import works.squirrel.field.databinding.ActivityQrBinding
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class QrScanActivity : AppCompatActivity() {
    private lateinit var binding: ActivityQrBinding
    private val executor = Executors.newSingleThreadExecutor()
    private val handled = AtomicBoolean(false)
    private var lastText: String = ""

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startCamera() else {
                Toast.makeText(this, R.string.camera_permission, Toast.LENGTH_LONG).show()
                finish()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityQrBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnBack.setOnClickListener { finish() }
        binding.btnCopy.setOnClickListener {
            if (lastText.isEmpty()) return@setOnClickListener
            val cm = getSystemService(ClipboardManager::class.java)
            cm.setPrimaryClip(ClipData.newPlainText("qr", lastText))
            Toast.makeText(this, R.string.copied, Toast.LENGTH_SHORT).show()
        }

        when {
            ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED -> startCamera()
            else -> permissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    private fun startCamera() {
        val providerFuture = ProcessCameraProvider.getInstance(this)
        providerFuture.addListener({
            val provider = providerFuture.get()
            val previewUseCase = Preview.Builder().build().also { useCase ->
                useCase.setSurfaceProvider(binding.preview.surfaceProvider)
            }
            val analysis = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()
            val scanner = BarcodeScanning.getClient()
            analysis.setAnalyzer(executor) { proxy ->
                val media = proxy.image
                if (media == null) {
                    proxy.close()
                    return@setAnalyzer
                }
                val image = InputImage.fromMediaImage(media, proxy.imageInfo.rotationDegrees)
                scanner.process(image)
                    .addOnSuccessListener { codes ->
                        val hit = codes.firstOrNull {
                            it.valueType != Barcode.TYPE_UNKNOWN && !it.rawValue.isNullOrBlank()
                        } ?: codes.firstOrNull { !it.rawValue.isNullOrBlank() }
                        val text = hit?.rawValue
                        if (!text.isNullOrBlank() && handled.compareAndSet(false, true)) {
                            lastText = text
                            runOnUiThread {
                                binding.qrResult.text = text
                                binding.btnCopy.isEnabled = true
                                binding.qrHint.setText(R.string.qr_found)
                            }
                        }
                    }
                    .addOnCompleteListener { proxy.close() }
            }
            provider.unbindAll()
            provider.bindToLifecycle(
                this, CameraSelector.DEFAULT_BACK_CAMERA, previewUseCase, analysis
            )
        }, ContextCompat.getMainExecutor(this))
    }

    override fun onDestroy() {
        executor.shutdown()
        super.onDestroy()
    }
}
