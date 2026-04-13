plugins { kotlin("multiplatform") }

repositories { mavenCentral() }

kotlin {
  jvm()

  sourceSets {
    commonMain.dependencies { implementation(project(":core")) }
    jvmMain.dependencies { implementation("com.squareup.okio:okio:3.9.1") }
  }
}
