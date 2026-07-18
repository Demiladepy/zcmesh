plugins {
    application
    id("org.openjfx.javafxplugin") version "0.1.0"
}

group = "zcmesh"
version = "1.0.0"

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(17))
    }
}

repositories {
    mavenCentral()
}

javafx {
    version = "21.0.2"
    modules("javafx.controls", "javafx.graphics")
}

application {
    mainClass.set("zcmesh.ui.OperatorApp")
}

tasks.register<JavaExec>("smoke") {
    group = "verification"
    description = "Headless NIO receiver for edge→operator smoke"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("zcmesh.smoke.HeadlessOperator")
    val smokeArgs = mutableListOf(
        project.findProperty("smokePort")?.toString() ?: "9900",
        project.findProperty("smokeFrames")?.toString() ?: "200",
        project.findProperty("smokeTimeout")?.toString() ?: "60"
    )
    project.findProperty("smokeRecord")?.toString()?.let { smokeArgs.add(it) }
    args(smokeArgs)
}

tasks.register<JavaExec>("golden") {
    group = "verification"
    description = "Cross-language wire CRC golden vector"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("zcmesh.wire.WireFrameGolden")
}

tasks.register<JavaExec>("zcmRoundtrip") {
    group = "verification"
    description = "Java .zcm write/read roundtrip"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("zcmesh.wire.ZcmRoundtrip")
}

tasks.register<JavaExec>("streamResync") {
    group = "verification"
    description = "TCP magic-scan resync self-check"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("zcmesh.net.StreamResyncCheck")
}

tasks.register("verifyAll") {
    group = "verification"
    dependsOn("golden", "zcmRoundtrip", "streamResync")
}

tasks.withType<JavaCompile> {
    options.encoding = "UTF-8"
}
