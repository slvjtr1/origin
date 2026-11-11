import java.io.File
import java.util.Properties
import java.security.MessageDigest

plugins { id("com.android.application") }

val repo = rootDir.resolve("../..").canonicalFile
val ver = Regex("\"version\"\\s*:\\s*\"([^\"]+)\"").find(repo.resolve("pkg/appinfo.json").readText())!!.groupValues[1]
val appId = "app.stremioorigin.tv"   // the in-app updater looks for <appId>_<version>.apk in the releases
// the built-in keys: the private file outside the repository (as tools/build.sh reads it); without it, none
val keys = System.getenv("KEYS") ?: ((System.getenv("ORIGIN_BUILD") ?: (System.getProperty("user.home") + "/origin-build")) + "/keys.env")

// sbkeys.h, as tools/sbkeys.py makes it: each key XORed with a pad (SHA-256 of "sb:<name>"), never plain text in the library
val keysDir = layout.buildDirectory.dir("gen/sbkeys").get().asFile
run {
    val kv = HashMap<String, String>(); val kf = File(keys)
    if (kf.isFile) kf.readLines().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") && it.contains('=') }
        .forEach { val k = it.substringBefore('=').trim(); kv[k] = it.substringAfter('=').trim().trim('"', '\'') }
    fun arr(name: String): String {
        val v = (kv[name] ?: "").toByteArray(); val pad = MessageDigest.getInstance("SHA-256").digest("sb:$name".toByteArray())
        val enc = v.mapIndexed { i, c -> ((c.toInt() and 255) xor (pad[i % 32].toInt() and 255)).toString() + "," }.joinToString("")
        return "static const unsigned char SBK_${name}[] = { ${enc}0 }, SBK_${name}_PAD[32] = { ${pad.joinToString(",") { (it.toInt() and 255).toString() }} };\n#define SBK_${name}_LEN ${v.size}\n"
    }
    val body = "/* made by app/build.gradle.kts from the private keys file: never commit */\n" + arr("TMDB_API_KEY")
    keysDir.mkdirs(); val h = File(keysDir, "sbkeys.h"); if (!h.isFile || h.readText() != body) h.writeText(body)
}

// release signing: ORIGIN_SIGNING names a properties file (storeFile, storePassword, keyAlias, keyPassword) kept out of the repository
val signing = System.getenv("ORIGIN_SIGNING")?.let { File(it) }?.takeIf { it.isFile }?.let { f -> Properties().apply { f.inputStream().use { load(it) } } }

android {
    namespace = "app.stremioorigin"
    compileSdk = 36
    ndkVersion = "29.0.14206865"
    defaultConfig {
        applicationId = appId
        minSdk = 28            // Android TV 9: the TVs and boxes of 2020 on (Google TV is Android 10 and later)
        targetSdk = 36
        versionName = ver
        versionCode = 30000 + ver.split(".").fold(0) { a, p -> a * 100 + p.toInt() } // (above every build made before 1.0.0: it installs over them)
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }   // (most TVs run 32-bit Android on 64-bit chips)
        externalNativeBuild { cmake { arguments += listOf("-DORIGIN_ROOT=${repo.invariantSeparatorsPath}", "-DANDROID_STL=c++_static", "-DORIGIN_APP_ID=$appId", "-DORIGIN_KEYS_DIR=${keysDir.invariantSeparatorsPath}") } }
    }
    buildFeatures { buildConfig = true }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.31.6" } }
    sourceSets["main"].assets.srcDirs(file("build/gen/assets"))
    sourceSets["main"].jniLibs.srcDirs(repo.resolve("build/android"))   // libsbengine.so per ABI (tools/build-engine.sh android-arm64 / android-armv7)
    packaging { jniLibs { useLegacyPackaging = true } }   // compressed in the APK (VLC's libraries are large), unpacked once at install
    if (signing != null) signingConfigs.create("release") {
        storeFile = File(signing.getProperty("storeFile")); storePassword = signing.getProperty("storePassword")
        keyAlias = signing.getProperty("keyAlias"); keyPassword = signing.getProperty("keyPassword")
    }
    buildTypes { release { isMinifyEnabled = false; if (signing != null) signingConfig = signingConfigs.getByName("release") } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}

dependencies {
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.media3:media3-exoplayer:1.5.1")
    implementation("androidx.media3:media3-exoplayer-hls:1.5.1")
    implementation("androidx.media3:media3-exoplayer-dash:1.5.1")
    implementation("org.jellyfin.media3:media3-ffmpeg-decoder:1.5.0+1")   // the app's own sound decoders (Dolby, DTS, TrueHD...) for devices without them
    implementation("androidx.media3:media3-effect:1.5.1")   // Upscale: frames through the graphics chip (OriginSharp)
    implementation("org.videolan.android:libvlc-all:3.6.2")   // the second player (OriginVlc): what ExoPlayer can't play here
}

// the repository's assets/ into the package, with assets.lst (the version, then every file) for the first start to unpack
val genAssets by tasks.registering {
    val src = repo.resolve("assets"); val out = layout.buildDirectory.dir("gen/assets")
    inputs.dir(src); inputs.file(repo.resolve("pkg/appinfo.json")); outputs.dir(out)
    doLast {
        val o = out.get().asFile; o.deleteRecursively(); o.mkdirs()
        src.copyRecursively(o, overwrite = true)
        val files = o.walkTopDown().filter { it.isFile }.map { it.relativeTo(o).invariantSeparatorsPath }.sorted().toList()
        File(o, "assets.lst").writeText(ver + "-" + files.size + "\n" + files.joinToString("\n") + "\n")
    }
}
tasks.named("preBuild") { dependsOn(genAssets) }
