plugins { kotlin("jvm") version "2.0.21" }

repositories { mavenCentral() }

dependencies { implementation(files("../core/build/libs/core.jar")) }
