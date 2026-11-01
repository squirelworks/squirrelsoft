package works.squirrel.field

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import works.squirrel.field.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.cardSpectrum.setOnClickListener {
            startActivity(Intent(this, SpectrumActivity::class.java))
        }
        binding.cardQr.setOnClickListener {
            startActivity(Intent(this, QrScanActivity::class.java))
        }
        binding.cardSuica.setOnClickListener {
            startActivity(Intent(this, SuicaActivity::class.java))
        }
        binding.cardWeb.setOnClickListener {
            startActivity(Intent(this, WebHubActivity::class.java).putExtra(
                WebHubActivity.EXTRA_PAGE, "index.html"
            ))
        }
    }
}
