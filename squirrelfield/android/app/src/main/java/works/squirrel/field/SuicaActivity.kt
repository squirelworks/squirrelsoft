package works.squirrel.field

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import works.squirrel.field.databinding.ActivitySuicaBinding

class SuicaActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySuicaBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySuicaBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnOpenSuica.setOnClickListener {
            val launch = packageManager.getLaunchIntentForPackage("works.squirrel.suica")
            if (launch != null) {
                startActivity(launch)
            } else {
                Toast.makeText(this, R.string.suica_missing, Toast.LENGTH_LONG).show()
            }
        }
        binding.btnDemoWeb.setOnClickListener {
            startActivity(
                Intent(this, WebHubActivity::class.java)
                    .putExtra(WebHubActivity.EXTRA_PAGE, "suica.html")
            )
        }
        binding.btnBack.setOnClickListener { finish() }
    }
}
