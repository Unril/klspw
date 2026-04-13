// init.gradle.kts
import groovy.json.JsonOutput
import java.io.File
import org.gradle.api.Project
import org.gradle.api.attributes.Bundling
import org.gradle.api.attributes.Category
import org.gradle.api.attributes.DocsType
import org.gradle.api.plugins.JavaPluginExtension
import org.gradle.api.tasks.SourceSet

fun File.safeCanonicalPath(): String =
  runCatching { canonicalFile.absolutePath }.getOrElse { absolutePath }

fun Iterable<File>.toSortedPaths(): List<String> =
  map { it.safeCanonicalPath() }.distinct().sorted()

// Build output directory, compatible with Gradle 6.x+ (buildDir) and 7.1+ (layout.buildDirectory).
fun Project.buildOutputDir(): File =
  runCatching { layout.buildDirectory.asFile.get() }.getOrElse { @Suppress("DEPRECATION") buildDir }

// Discover kapt/ksp generated source directories for a source set.
// These are added by annotation processors but may not appear in allSource.srcDirs.
fun Project.generatedSourceDirs(sourceSetName: String): List<String> {
  val dirs = mutableListOf<File>()
  val bd = buildOutputDir()

  // kapt: build/generated/source/kapt/{sourceSetName}
  val kaptDir = file("${bd}/generated/source/kapt/$sourceSetName")
  if (kaptDir.isDirectory) dirs.add(kaptDir)

  // ksp: build/generated/ksp/{sourceSetName}
  val kspDir = file("${bd}/generated/ksp/$sourceSetName")
  if (kspDir.isDirectory) dirs.add(kspDir)

  return dirs.toSortedPaths()
}

// Resolve source jars for a configuration using Gradle's artifact view API.
// Returns a map from classes jar path to source jar path.
fun Project.resolveSourceJars(configName: String): Map<String, String> {
  val config = configurations.findByName(configName) ?: return emptyMap()
  if (!config.isCanBeResolved) return emptyMap()

  return runCatching {
      val sourceArtifacts =
        config.incoming
          .artifactView {
            withVariantReselection()
            attributes {
              attribute(
                Category.CATEGORY_ATTRIBUTE,
                objects.named(Category::class.java, Category.DOCUMENTATION),
              )
              attribute(
                DocsType.DOCS_TYPE_ATTRIBUTE,
                objects.named(DocsType::class.java, DocsType.SOURCES),
              )
              attribute(
                Bundling.BUNDLING_ATTRIBUTE,
                objects.named(Bundling::class.java, Bundling.EXTERNAL),
              )
            }
          }
          .artifacts
          .artifacts

      val sourcesByBaseName = mutableMapOf<String, String>()
      for (artifact in sourceArtifacts) {
        val name = artifact.file.name
        val baseName =
          if (name.endsWith("-sources.jar")) {
            name.removeSuffix("-sources.jar")
          } else {
            artifact.file.nameWithoutExtension
          }
        sourcesByBaseName[baseName] = artifact.file.safeCanonicalPath()
      }

      val classesArtifacts = config.incoming.artifacts.artifacts
      val result = mutableMapOf<String, String>()
      for (artifact in classesArtifacts) {
        val classesPath = artifact.file.safeCanonicalPath()
        val baseName = artifact.file.nameWithoutExtension
        sourcesByBaseName[baseName]?.let { result[classesPath] = it }
      }
      result
    }
    .getOrElse { e ->
      logger.warn("klspw: failed to resolve source jars for $configName: ${e.message}")
      emptyMap()
    }
}

fun SourceSet.toModel(project: Project): Map<String, Any?> {
  val generatedSources = project.generatedSourceDirs(name)
  val allSourceRoots = (allSource.srcDirs.toSortedPaths() + generatedSources).distinct().sorted()
  val allJavaRoots = (java.srcDirs.toSortedPaths() + generatedSources).distinct().sorted()

  // Resolve classpaths. Try the SourceSet's FileCollection first (works on most Gradle versions).
  // Fall back to resolving the configuration by name if the FileCollection returns empty
  // (Gradle 9.x lazy evaluation issue with per-project tasks).
  val compileCpFromFileCollection =
    runCatching { compileClasspath.files.toSortedPaths() }.getOrDefault(emptyList())
  val compileCp = compileCpFromFileCollection.ifEmpty {
    runCatching {
        val configName = compileClasspathConfigurationName
        // Gradle 9.4 may prefix main source set config names; try unprefixed fallback.
        val config =
          project.configurations.findByName(configName)
            ?: project.configurations.findByName(
              configName.removePrefix("main").replaceFirstChar { it.lowercase() }
            )
        config?.takeIf { it.isCanBeResolved }?.resolve()?.toSortedPaths() ?: emptyList()
      }
      .getOrElse { e ->
        project.logger.warn(
          "klspw: failed to resolve compileClasspath for source set '$name': ${e.message}"
        )
        emptyList()
      }
  }
  val runtimeCpFromFileCollection =
    runCatching { runtimeClasspath.files.toSortedPaths() }.getOrDefault(emptyList())
  val runtimeCp = runtimeCpFromFileCollection.ifEmpty {
    runCatching {
        val configName = runtimeClasspathConfigurationName
        val config =
          project.configurations.findByName(configName)
            ?: project.configurations.findByName(
              configName.removePrefix("main").replaceFirstChar { it.lowercase() }
            )
        config?.takeIf { it.isCanBeResolved }?.resolve()?.toSortedPaths() ?: emptyList()
      }
      .getOrElse { e ->
        project.logger.warn(
          "klspw: failed to resolve runtimeClasspath for source set '$name': ${e.message}"
        )
        emptyList()
      }
  }
  val sourceJarMap = project.resolveSourceJars(compileClasspathConfigurationName)

  return mapOf(
    "name" to name,
    "sourceRoots" to allSourceRoots,
    "javaSourceRoots" to allJavaRoots,
    "resourcesRoots" to resources.srcDirs.toSortedPaths(),
    "classesDirs" to output.classesDirs.files.toSortedPaths(),
    "resourcesDir" to output.resourcesDir?.safeCanonicalPath(),
    "compileClasspath" to compileCp,
    "runtimeClasspath" to runtimeCp,
    "sourceClasspath" to sourceJarMap,
    "compileClasspathConfigurationName" to compileClasspathConfigurationName,
    "runtimeClasspathConfigurationName" to runtimeClasspathConfigurationName,
  )
}

// Safely read a property from an object via Groovy dynamic dispatch,
// falling back to Java getter reflection. Works with both Groovy and plain Java/Kotlin objects.
fun Any.safeProp(name: String): Any? {
  // Try Groovy dynamic dispatch first
  runCatching { (this as groovy.lang.GroovyObject).getProperty(name) }
    .getOrNull()
    ?.let {
      return it
    }
  // Fall back to Java getter: getXxx()
  val getter = "get${name.replaceFirstChar { it.uppercase() }}"
  return runCatching { this.javaClass.getMethod(getter).invoke(this) }.getOrNull()
}

// Safely extract srcDirs (Set<File>) from an Android source provider.
fun Any?.srcDirs(): List<File> {
  if (this == null) return emptyList()
  return runCatching {
      @Suppress("UNCHECKED_CAST")
      val dirs = safeProp("srcDirs") as? Collection<*> ?: return@runCatching emptyList()
      dirs.filterIsInstance<File>()
    }
    .getOrDefault(emptyList())
}

// Resolve classpath files for an Android configuration using artifactView.
// Requests "android-classes-jar" artifact type so AGP's transforms extract classes.jar
// from AARs. Falls back to lenient raw file resolution if the transform isn't available.
fun Project.resolveAndroidClasspath(configName: String): List<String> {
  val config = configurations.findByName(configName) ?: return emptyList()
  if (!config.isCanBeResolved) return emptyList()

  // Try AGP's classes-jar transform first (extracts classes.jar from AARs).
  val transformed =
    runCatching {
        config.incoming
          .artifactView {
            attributes {
              attribute(
                org.gradle.api.attributes.Attribute.of("artifactType", String::class.java),
                "android-classes-jar",
              )
            }
          }
          .files
          .toSortedPaths()
      }
      .getOrNull()

  if (transformed != null && transformed.isNotEmpty()) return transformed

  // Fallback: lenient resolution (returns AARs + JARs as-is).
  return runCatching { config.incoming.artifactView { lenient(true) }.files.toSortedPaths() }
    .getOrElse { e ->
      logger.warn("klspw: failed to resolve $configName: ${e.message}")
      emptyList()
    }
}

// Extract source sets from an Android extension (Library/Application/DynamicFeature).
// Emits one entry per build variant (debug, release, etc.) with merged source roots
// (main + variant) and the variant's compile/runtime classpath.
// Falls back to per-source-set output if variant extraction fails.
fun Project.extractAndroidSourceSets(androidExt: Any): List<Map<String, Any?>> {
  return runCatching {
      @Suppress("UNCHECKED_CAST")
      val container =
        androidExt.safeProp("sourceSets") as? org.gradle.api.NamedDomainObjectContainer<*>
          ?: return@runCatching emptyList()

      // Collect source dirs per source set name for merging.
      val sourceSetMap = mutableMapOf<String, Triple<List<File>, List<File>, List<File>>>()
      for (sourceSet in container) {
        val name = sourceSet.safeProp("name") as? String ?: continue
        val javaDirs = sourceSet.safeProp("java").srcDirs()
        val kotlinDirs = sourceSet.safeProp("kotlin").srcDirs()
        val resDirs = sourceSet.safeProp("res").srcDirs()
        sourceSetMap[name] = Triple(javaDirs, kotlinDirs, resDirs)
      }

      val mainJava = sourceSetMap["main"]?.first ?: emptyList()
      val mainKotlin = sourceSetMap["main"]?.second ?: emptyList()
      val mainRes = sourceSetMap["main"]?.third ?: emptyList()

      // Android SDK boot classpath (android.jar) — needed for android.os.Bundle, etc.
      @Suppress("UNCHECKED_CAST")
      val bootClasspath =
        runCatching {
            (androidExt.safeProp("bootClasspath") as? Collection<*>)
              ?.filterIsInstance<File>()
              ?.toSortedPaths() ?: emptyList()
          }
          .getOrElse { e ->
            logger.warn("klspw: failed to resolve Android bootClasspath: ${e.message}")
            emptyList()
          }

      // Find build variants by looking for {variant}CompileClasspath configurations.
      // Emits: main variants (debug, release) and unit test variants (testDebug, testRelease).
      val allConfigNames =
        configurations.names
          .filter { it.endsWith("CompileClasspath") }
          .map { it.removeSuffix("CompileClasspath") }

      val mainVariants =
        allConfigNames
          .filter { name ->
            name != "main" &&
              !name.contains("UnitTest") &&
              !name.contains("AndroidTest") &&
              !name.startsWith("test") &&
              !name.startsWith("androidTest") &&
              !name.startsWith("testFixtures") &&
              sourceSetMap.containsKey(name)
          }
          .sorted()

      // Unit test variants: debugUnitTest, releaseUnitTest.
      // Merge test + main + baseVariant sources with the unit test classpath.
      val unitTestVariants =
        allConfigNames.filter { it.endsWith("UnitTest") && !it.contains("AndroidTest") }.sorted()

      // If no variants found, fall back to emitting main source set only.
      if (mainVariants.isEmpty() && sourceSetMap.containsKey("main")) {
        val generated = generatedSourceDirs("main")
        val allSrc = (mainJava + mainKotlin).toSortedPaths().plus(generated).distinct().sorted()
        return@runCatching listOf(
          mapOf(
            "name" to "main",
            "sourceRoots" to allSrc,
            "javaSourceRoots" to mainJava.toSortedPaths().plus(generated).distinct().sorted(),
            "resourcesRoots" to mainRes.toSortedPaths(),
            "classesDirs" to emptyList<String>(),
            "resourcesDir" to null,
            "compileClasspath" to emptyList<String>(),
            "runtimeClasspath" to emptyList<String>(),
            "sourceClasspath" to emptyMap<String, String>(),
            "compileClasspathConfigurationName" to "",
            "runtimeClasspathConfigurationName" to "",
          )
        )
      }

      val mainEntries = mainVariants.map { variant ->
        val variantJava = sourceSetMap[variant]?.first ?: emptyList()
        val variantKotlin = sourceSetMap[variant]?.second ?: emptyList()
        val variantRes = sourceSetMap[variant]?.third ?: emptyList()

        val generated = generatedSourceDirs(variant)
        val mergedJava = (mainJava + variantJava)
        val mergedKotlin = (mainKotlin + variantKotlin)
        val mergedRes = (mainRes + variantRes)

        val allSourceRoots =
          (mergedJava + mergedKotlin).toSortedPaths().plus(generated).distinct().sorted()
        val javaSourceRoots = mergedJava.toSortedPaths().plus(generated).distinct().sorted()

        val configName = "${variant}CompileClasspath"
        val runtimeConfigName = "${variant}RuntimeClasspath"

        mapOf(
          "name" to variant,
          "sourceRoots" to allSourceRoots,
          "javaSourceRoots" to javaSourceRoots,
          "resourcesRoots" to mergedRes.toSortedPaths(),
          "classesDirs" to emptyList<String>(),
          "resourcesDir" to null,
          "compileClasspath" to
            (bootClasspath + resolveAndroidClasspath(configName)).distinct().sorted(),
          "runtimeClasspath" to resolveAndroidClasspath(runtimeConfigName),
          "sourceClasspath" to resolveSourceJars(configName),
          "compileClasspathConfigurationName" to configName,
          "runtimeClasspathConfigurationName" to runtimeConfigName,
        )
      }

      // Test variant entries: merge test + main + baseVariant sources with unit test classpath.
      // e.g. debugUnitTest = test + main + debug sources, debugUnitTestCompileClasspath.
      val testEntries = unitTestVariants.mapNotNull { testVariant ->
        runCatching {
            // debugUnitTest -> debug
            val baseVariant = testVariant.removeSuffix("UnitTest")
            if (!sourceSetMap.containsKey(baseVariant)) {
              logger.warn(
                "klspw: no source set for base variant '$baseVariant' (test variant '$testVariant')"
              )
            }

            val testSrcJava = sourceSetMap["test"]?.first ?: emptyList()
            val testSrcKotlin = sourceSetMap["test"]?.second ?: emptyList()
            val testSrcRes = sourceSetMap["test"]?.third ?: emptyList()
            val baseJava = sourceSetMap[baseVariant]?.first ?: emptyList()
            val baseKotlin = sourceSetMap[baseVariant]?.second ?: emptyList()

            val generated = generatedSourceDirs(testVariant) + generatedSourceDirs("test")
            val mergedJava = (mainJava + baseJava + testSrcJava)
            val mergedKotlin = (mainKotlin + baseKotlin + testSrcKotlin)
            val mergedRes = (mainRes + testSrcRes)

            val allSourceRoots =
              (mergedJava + mergedKotlin).toSortedPaths().plus(generated).distinct().sorted()
            val javaSourceRoots = mergedJava.toSortedPaths().plus(generated).distinct().sorted()

            val configName = "${testVariant}CompileClasspath"
            val runtimeConfigName = "${testVariant}RuntimeClasspath"

            mapOf(
              "name" to testVariant,
              "sourceRoots" to allSourceRoots,
              "javaSourceRoots" to javaSourceRoots,
              "resourcesRoots" to mergedRes.toSortedPaths(),
              "classesDirs" to emptyList<String>(),
              "resourcesDir" to null,
              "compileClasspath" to
                (bootClasspath + resolveAndroidClasspath(configName)).distinct().sorted(),
              "runtimeClasspath" to resolveAndroidClasspath(runtimeConfigName),
              "sourceClasspath" to resolveSourceJars(configName),
              "compileClasspathConfigurationName" to configName,
              "runtimeClasspathConfigurationName" to runtimeConfigName,
            )
          }
          .getOrElse { e ->
            logger.warn("klspw: failed to extract test variant '$testVariant': ${e.message}")
            null
          }
      }

      mainEntries + testEntries
    }
    .getOrElse { e ->
      logger.warn("klspw: failed to extract Android source sets: ${e.message}")
      emptyList()
    }
}

// Extract Kotlin Multiplatform source sets from the kotlin extension.
// Returns a map of source set name -> list of source directories.
// Works for KMP projects with commonMain, androidMain, iosMain, etc.
fun Project.extractKmpSourceSets(): Map<String, List<File>> {
  val kotlinExt = extensions.findByName("kotlin") ?: return emptyMap()
  return runCatching {
      @Suppress("UNCHECKED_CAST")
      val sourceSets =
        kotlinExt.safeProp("sourceSets") as? org.gradle.api.NamedDomainObjectContainer<*>
          ?: return@runCatching emptyMap()

      sourceSets
        .associate { sourceSet ->
          val name = sourceSet.safeProp("name") as? String ?: ""
          val kotlinDirs = sourceSet.safeProp("kotlin").srcDirs()
          name to kotlinDirs
        }
        .filterKeys { it.isNotEmpty() }
    }
    .getOrDefault(emptyMap())
}

fun Project.detectModel(): Map<String, Any?> {
  val pluginClasses = plugins.map { it.javaClass.name }.sorted()
  val kmpSourceSets = extractKmpSourceSets()

  // Android projects: extract source sets from the Android extension.
  // For KMP+Android projects, also merge commonMain/androidMain KMP source sets.
  val androidExt = extensions.findByName("android")
  if (androidExt != null) {
    val sourceSets = extractAndroidSourceSets(androidExt)

    // Merge KMP source sets (commonMain, androidMain, etc.) into each Android variant.
    val kmpMainDirs =
      (kmpSourceSets["commonMain"] ?: emptyList()) + (kmpSourceSets["androidMain"] ?: emptyList())
    val kmpTestDirs =
      (kmpSourceSets["commonTest"] ?: emptyList()) +
        (kmpSourceSets["androidUnitTest"] ?: emptyList())

    val mergedSourceSets =
      if (kmpMainDirs.isNotEmpty() || kmpTestDirs.isNotEmpty()) {
        sourceSets.map { ss ->
          @Suppress("UNCHECKED_CAST") val name = ss["name"] as? String ?: ""
          val isTest = name.contains("UnitTest", ignoreCase = true)
          val extraDirs = if (isTest) (kmpMainDirs + kmpTestDirs) else kmpMainDirs

          if (extraDirs.isEmpty()) return@map ss

          @Suppress("UNCHECKED_CAST")
          val existingRoots = ss["sourceRoots"] as? List<String> ?: emptyList()
          @Suppress("UNCHECKED_CAST")
          val existingJavaRoots = ss["javaSourceRoots"] as? List<String> ?: emptyList()

          ss +
            mapOf(
              "sourceRoots" to (existingRoots + extraDirs.toSortedPaths()).distinct().sorted(),
              "javaSourceRoots" to
                (existingJavaRoots + extraDirs.toSortedPaths()).distinct().sorted(),
            )
        }
      } else {
        sourceSets
      }

    return mapOf(
      "projectPath" to path,
      "projectName" to name,
      "projectDir" to projectDir.safeCanonicalPath(),
      "kind" to if (kmpSourceSets.isNotEmpty()) "kmp-android" else "android",
      "plugins" to pluginClasses,
      "sourceSets" to mergedSourceSets,
    )
  }

  // Pure KMP without Android (e.g., commonMain + jvmMain, or JS/Native targets).
  // Must check before JVM because KMP applies JavaBasePlugin with empty sourceSets.
  // Only trigger for actual multiplatform projects, not kotlin("jvm") which also has
  // kotlin.sourceSets.
  // KMP config naming: {targetName}CompileClasspath (e.g., jvmCompileClasspath),
  // NOT {sourceSetName}CompileClasspath. commonMain has no resolvable classpath —
  // its deps are resolved through target-specific configurations.
  val isMultiplatform = pluginClasses.any { it.contains("KotlinMultiplatformPlugin") }
  if (isMultiplatform && kmpSourceSets.isNotEmpty() && extensions.findByName("android") == null) {
    // Build a map from source set name to the best resolvable configuration.
    // jvmMain -> jvmCompileClasspath, jvmTest -> jvmTestCompileClasspath,
    // commonMain -> metadataCompileClasspath (if available), etc.
    val resolvableConfigs =
      configurations.names
        .filter { it.endsWith("CompileClasspath") }
        .filter { name -> configurations.findByName(name)?.isCanBeResolved == true }
        .associateBy { it.removeSuffix("CompileClasspath") }

    val kmpEntries =
      kmpSourceSets.entries
        .sortedBy { it.key }
        .map { (name, dirs) ->
          val generated = generatedSourceDirs(name)

          // Map source set name to target config: jvmMain -> jvm, jvmTest -> jvmTest,
          // commonMain -> metadata (fallback)
          val configKey = name.removeSuffix("Main")
          val configName =
            resolvableConfigs[configKey]
              ?: resolvableConfigs[name]
              ?: resolvableConfigs["metadata"]?.takeIf { name.startsWith("common") }
              ?: ""
          val runtimeKey = configKey
          val runtimeConfigName = configName.replace("CompileClasspath", "RuntimeClasspath")

          mapOf(
            "name" to name,
            "sourceRoots" to (dirs.toSortedPaths() + generated).distinct().sorted(),
            "javaSourceRoots" to generated.distinct().sorted(),
            "resourcesRoots" to emptyList<String>(),
            "classesDirs" to emptyList<String>(),
            "resourcesDir" to null,
            "compileClasspath" to
              if (configName.isNotEmpty()) {
                runCatching { configurations.getByName(configName).resolve().toSortedPaths() }
                  .getOrElse { e ->
                    logger.debug("klspw: KMP config '$configName' not resolvable: ${e.message}")
                    emptyList()
                  }
              } else emptyList(),
            "runtimeClasspath" to
              if (runtimeConfigName.isNotEmpty()) {
                runCatching {
                    configurations.getByName(runtimeConfigName).resolve().toSortedPaths()
                  }
                  .getOrElse { e ->
                    logger.debug(
                      "klspw: KMP config '$runtimeConfigName' not resolvable: ${e.message}"
                    )
                    emptyList()
                  }
              } else emptyList(),
            "sourceClasspath" to
              if (configName.isNotEmpty()) resolveSourceJars(configName) else emptyMap(),
            "compileClasspathConfigurationName" to configName,
            "runtimeClasspathConfigurationName" to runtimeConfigName,
          )
        }

    return mapOf(
      "projectPath" to path,
      "projectName" to name,
      "projectDir" to projectDir.safeCanonicalPath(),
      "kind" to "kmp",
      "plugins" to pluginClasses,
      "sourceSets" to kmpEntries,
    )
  }

  // Standard JVM projects: use JavaPluginExtension source sets.
  val javaExt = extensions.findByType(JavaPluginExtension::class.java)
  if (javaExt != null) {
    val sourceSets = javaExt.sourceSets.sortedBy { it.name }.map { it.toModel(this) }

    return mapOf(
      "projectPath" to path,
      "projectName" to name,
      "projectDir" to projectDir.safeCanonicalPath(),
      "kind" to "jvm",
      "plugins" to pluginClasses,
      "sourceSets" to sourceSets,
    )
  }

  return mapOf(
    "projectPath" to path,
    "projectName" to name,
    "projectDir" to projectDir.safeCanonicalPath(),
    "kind" to "non-jvm",
    "plugins" to pluginClasses,
    "sourceSets" to emptyList<Map<String, Any?>>(),
    "skipReason" to "No Java, Android, or Kotlin Multiplatform extensions found.",
  )
}

// Per-project task approach: each project registers its own klspwDumpModel task that
// resolves configurations in its own execution context. This respects Gradle 9 project
// isolation — no cross-project configuration access needed.
// The root dumpKotlinLspModel task depends on all per-project tasks and collects their output.
// --no-configuration-cache is passed by klspw to avoid serialization issues.
val metadataDir = java.nio.file.Files.createTempDirectory("klspw-metadata-").toFile()

allprojects {
  if (gradle.parent != null) return@allprojects // skip included builds

  val proj = this
  tasks.register("klspwDumpModel") {
    val outputFile = File(metadataDir, "${proj.path.replace(':', '_').trimStart('_')}.json")
    outputs.file(outputFile)

    doLast {
      val model = proj.detectModel()
      val json = JsonOutput.toJson(model)
      outputFile.parentFile.mkdirs()
      outputFile.writeText(json)
    }
  }
}

gradle.projectsEvaluated {
  rootProject.tasks.register("dumpKotlinLspModel") {
    group = "kotlin lsp"
    description = "Dumps a JSON model for JVM/Kotlin-JVM projects in this build"

    val modelTasks = rootProject.allprojects.mapNotNull { it.tasks.findByName("klspwDumpModel") }
    dependsOn(modelTasks)

    doLast {
      val projectsModel =
        if (metadataDir.isDirectory) {
          metadataDir
            .listFiles()
            ?.filter { it.extension == "json" }
            ?.sorted()
            ?.mapNotNull { file ->
              runCatching {
                  @Suppress("UNCHECKED_CAST")
                  groovy.json.JsonSlurper().parseText(file.readText()) as Map<String, Any?>
                }
                .getOrElse { e ->
                  logger.warn("klspw: failed to parse metadata from ${file.name}: ${e.message}")
                  null
                }
            } ?: emptyList()
        } else {
          emptyList()
        }

      val sorted = projectsModel.sortedBy { it["projectPath"] as? String ?: "" }
      val result =
        mapOf("rootProject" to rootProject.projectDir.safeCanonicalPath(), "projects" to sorted)

      println("KLSPW_BEGIN")
      println(JsonOutput.prettyPrint(JsonOutput.toJson(result)))
      println("KLSPW_END")

      metadataDir.deleteRecursively()
    }
  }
}
