import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "works.squirrel.qr"
    compileSdk = 34

    defaultConfig {
        applicationId = "works.squirrel.qr"
        minSdk = 26
        targetSdk = 34
        versionCode = 4
        versionName = "0.1.1"
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
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.activity:activity-ktx:1.9.1")

    implementation("androidx.camera:camera-camera2:1.3.4")
    implementation("androidx.camera:camera-lifecycle:1.3.4")
    implementation("androidx.camera:camera-view:1.3.4")
    implementation("com.google.mlkit:barcode-scanning:17.2.0")

    testImplementation("junit:junit:4.13.2")
}
