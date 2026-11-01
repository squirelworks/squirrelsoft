package works.squirrel.field

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity

/** Opens the bundled live-spectrum PWA page (mic + waterfall). */
class SpectrumActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startActivity(
            Intent(this, WebHubActivity::class.java)
                .putExtra(WebHubActivity.EXTRA_PAGE, "spectrum.html")
        )
        finish()
    }
}
