import org.gradle.internal.os.OperatingSystem

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.kapt")
}

android {
    namespace = "com.mycelia.security"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.mycelia.security"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    sourceSets {
        getByName("main") {
            shaders.setSrcDirs(listOf<String>())
        }
    }
}

val shaderSourceDir = file("../shaders_src")
val shaderAssetDir = file("src/main/assets/shaders")
val shaderFiles = listOf(
    "mycelia_keystream_xor.comp",
    "mycelia_xor.comp",
    "subqg_init.comp",
    "subqg_simulation.comp"
)

fun resolveGlslcPath(): File {
    val sdkDir = android.sdkDirectory
    val ndkDir = android.ndkDirectory ?: File(sdkDir, "ndk/${android.ndkVersion}")
    val os = OperatingSystem.current()
    val hostTag = when {
        os.isWindows -> "windows-x86_64"
        os.isMacOsX -> "darwin-x86_64"
        else -> "linux-x86_64"
    }
    val exeName = if (os.isWindows) "glslc.exe" else "glslc"
    return File(ndkDir, "shader-tools/$hostTag/$exeName")
}

val compileMyceliaShaders = tasks.register("compileMyceliaShaders") {
    description = "Compile Mycelia GLSL compute shaders into SPIR-V assets."
    group = "build"
    inputs.files(shaderFiles.map { File(shaderSourceDir, it) })
    outputs.files(shaderFiles.map { File(shaderAssetDir, "$it.spv") })
    doLast {
        val glslc = resolveGlslcPath()
        if (!glslc.exists()) {
            throw GradleException("glslc not found at ${glslc.absolutePath}. Ensure NDK ${android.ndkVersion} is installed.")
        }
        shaderAssetDir.mkdirs()
        shaderFiles.forEach { shaderName ->
            val input = File(shaderSourceDir, shaderName)
            val output = File(shaderAssetDir, "$shaderName.spv")
            exec {
                commandLine(glslc.absolutePath, input.absolutePath, "-o", output.absolutePath)
            }
        }
    }
}

tasks.named("preBuild") {
    dependsOn(compileMyceliaShaders)
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.06.00")
    implementation(composeBom)
    androidTestImplementation(composeBom)

    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.navigation:navigation-compose:2.7.7")
    implementation("com.google.android.material:material:1.11.0")

    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")

    implementation("androidx.room:room-runtime:2.6.1")
    implementation("androidx.room:room-ktx:2.6.1")
    kapt("androidx.room:room-compiler:2.6.1")

    implementation("androidx.datastore:datastore-preferences:1.1.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    implementation("com.google.zxing:core:3.5.3")
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
    implementation("org.bouncycastle:bcprov-jdk18on:1.78.1")
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
    implementation("net.zetetic:android-database-sqlcipher:4.5.4")
    implementation("androidx.sqlite:sqlite-ktx:2.4.0")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
}
