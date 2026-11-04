import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Play Store screenshots are rendered with Robolectric only when asked for
// (-PstoreShots); normal builds and CI never pull it in. See
// docs/play-graphics/suicaanalysis/README.md.
val storeShots = project.hasProperty("storeShots")

android {
    namespace = "works.squirrel.suicaanalysis"
    compileSdk = 36

    defaultConfig {
        applicationId = "works.squirrel.suicaanalysis"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.0.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    val localProps = Properties().apply {
        val f = rootProject.file("local.properties")
        if (f.exists()) {
            f.inputStream().use { load(it) }
        }
    }
    fun prop(name: String): String? =
        System.getenv(name)
            ?: (project.findProperty(name) as String?)
            ?: localProps.getProperty(name)

    val uploadStoreFile = prop("SQUIRREL_UPLOAD_STORE_FILE")
        ?: prop("squirrel.upload.storeFile")
    val uploadStorePassword = prop("SQUIRREL_UPLOAD_STORE_PASSWORD")
        ?: prop("squirrel.upload.storePassword")
    val uploadKeyAlias = prop("SQUIRREL_UPLOAD_KEY_ALIAS")
        ?: prop("squirrel.upload.keyAlias")
    val uploadKeyPassword = prop("SQUIRREL_UPLOAD_KEY_PASSWORD")
        ?: prop("squirrel.upload.keyPassword")
    val uploadStore = uploadStoreFile?.takeIf { it.isNotBlank() }?.let { file(it) }
    val hasUploadSigning = uploadStore != null &&
        uploadStore.isFile &&
        !uploadStorePassword.isNullOrBlank() &&
        !uploadKeyAlias.isNullOrBlank() &&
        !uploadKeyPassword.isNullOrBlank()

    if (hasUploadSigning) {
        signingConfigs {
            create("release") {
                storeFile = uploadStore
                storePassword = uploadStorePassword
                keyAlias = uploadKeyAlias
                keyPassword = uploadKeyPassword
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            if (hasUploadSigning) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        viewBinding = true
    }
    // History parsing uses the shared C core in libs/sqfelica through JNI.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
    if (storeShots) {
        testOptions.unitTests.isIncludeAndroidResources = true
        sourceSets["test"].java.srcDir("src/storeShots/java")
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.activity:activity-ktx:1.9.1")

    testImplementation("junit:junit:4.13.2")
    if (storeShots) {
        testImplementation("org.robolectric:robolectric:4.13")
    }
}
