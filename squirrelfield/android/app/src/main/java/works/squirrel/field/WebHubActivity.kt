package works.squirrel.field

import android.Manifest
import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.os.Bundle
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.webkit.WebViewAssetLoader
import works.squirrel.field.databinding.ActivityWebBinding

/** Serves bundled PWA pages from android_asset/web via WebViewAssetLoader. */
class WebHubActivity : AppCompatActivity() {
    companion object {
        const val EXTRA_PAGE = "page"
    }

    private lateinit var binding: ActivityWebBinding
    private var pendingPermissionRequest: PermissionRequest? = null

    private val audioPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
            val req = pendingPermissionRequest
            pendingPermissionRequest = null
            if (ok && req != null) {
                req.grant(req.resources)
            } else {
                req?.deny()
                Toast.makeText(this, R.string.mic_permission, Toast.LENGTH_LONG).show()
            }
        }

    private val cameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
            val req = pendingPermissionRequest
            pendingPermissionRequest = null
            if (ok && req != null) req.grant(req.resources) else req?.deny()
        }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWebBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val page = intent.getStringExtra(EXTRA_PAGE) ?: "index.html"
        binding.title.text = when {
            page.startsWith("spectrum") -> getString(R.string.spectrum_title)
            page.startsWith("qr") -> getString(R.string.qr_title)
            page.startsWith("suica") -> getString(R.string.suica_title)
            else -> getString(R.string.app_name)
        }
        binding.btnBack.setOnClickListener { finish() }

        val assetLoader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        binding.web.settings.javaScriptEnabled = true
        binding.web.settings.domStorageEnabled = true
        binding.web.settings.mediaPlaybackRequiresUserGesture = false
        binding.web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: WebResourceRequest
            ): WebResourceResponse? = assetLoader.shouldInterceptRequest(request.url)
        }
        binding.web.webChromeClient = object : WebChromeClient() {
            override fun onPermissionRequest(request: PermissionRequest) {
                val needsAudio = request.resources.any {
                    it == PermissionRequest.RESOURCE_AUDIO_CAPTURE
                }
                val needsVideo = request.resources.any {
                    it == PermissionRequest.RESOURCE_VIDEO_CAPTURE
                }
                when {
                    needsAudio && ContextCompat.checkSelfPermission(
                        this@WebHubActivity, Manifest.permission.RECORD_AUDIO
                    ) != PackageManager.PERMISSION_GRANTED -> {
                        pendingPermissionRequest = request
                        audioPermission.launch(Manifest.permission.RECORD_AUDIO)
                    }
                    needsVideo && ContextCompat.checkSelfPermission(
                        this@WebHubActivity, Manifest.permission.CAMERA
                    ) != PackageManager.PERMISSION_GRANTED -> {
                        pendingPermissionRequest = request
                        cameraPermission.launch(Manifest.permission.CAMERA)
                    }
                    else -> request.grant(request.resources)
                }
            }
        }

        binding.web.loadUrl("https://appassets.androidplatform.net/assets/web/$page")
    }

    override fun onDestroy() {
        binding.web.destroy()
        super.onDestroy()
    }
}
