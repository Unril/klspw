plugins {
    kotlin("jvm") version "2.0.21"
}

repositories {
    mavenCentral()
}

dependencies {
    implementation("com.google.guava:guava:33.4.0-jre")
    testImplementation(kotlin("test"))
}

tasks.test {
    useJUnitPlatform()
}
