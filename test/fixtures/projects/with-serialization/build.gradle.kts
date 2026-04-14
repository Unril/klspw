plugins {
  kotlin("jvm") version "2.0.21"
  kotlin("plugin.serialization") version "2.0.21"
}

repositories { mavenCentral() }

dependencies { implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3") }
