// init.gradle.kts
import groovy.json.JsonOutput
import org.gradle.api.Project
import org.gradle.api.plugins.JavaPluginExtension
import org.gradle.api.tasks.SourceSet
import java.io.File

fun File.safeCanonicalPath(): String =
    runCatching { canonicalFile.absolutePath }.getOrElse { absolutePath }

fun Iterable<File>.toSortedPaths(): List<String> =
    map { it.safeCanonicalPath() }.distinct().sorted()

/// Discover kapt/ksp generated source directories for a source set.
/// These are added by annotation processors but may not appear in allSource.srcDirs.
fun Project.generatedSourceDirs(sourceSetName: String): List<String> {
    val dirs = mutableListOf<File>()

    // kapt: build/generated/source/kapt/{sourceSetName}
    val kaptDir = file("${buildDir}/generated/source/kapt/$sourceSetName")
    if (kaptDir.isDirectory) dirs.add(kaptDir)

    // ksp: build/generated/ksp/{sourceSetName}
    val kspDir = file("${buildDir}/generated/ksp/$sourceSetName")
    if (kspDir.isDirectory) dirs.add(kspDir)

    return dirs.toSortedPaths()
}

fun SourceSet.toModel(project: Project): Map<String, Any?> {
    val generatedSources = project.generatedSourceDirs(name)
    val allSourceRoots = (allSource.srcDirs.toSortedPaths() + generatedSources).distinct().sorted()
    val allJavaRoots = (java.srcDirs.toSortedPaths() + generatedSources).distinct().sorted()

    return mapOf(
        "name" to name,
        "sourceRoots" to allSourceRoots,
        "javaSourceRoots" to allJavaRoots,
        "resourcesRoots" to resources.srcDirs.toSortedPaths(),
        "classesDirs" to output.classesDirs.files.toSortedPaths(),
        "resourcesDir" to output.resourcesDir?.safeCanonicalPath(),
        "compileClasspath" to runCatching { compileClasspath.files.toSortedPaths() }.getOrDefault(emptyList()),
        "runtimeClasspath" to runCatching { runtimeClasspath.files.toSortedPaths() }.getOrDefault(emptyList()),
        "compileClasspathConfigurationName" to compileClasspathConfigurationName,
        "runtimeClasspathConfigurationName" to runtimeClasspathConfigurationName
    )
}

fun Project.detectModel(): Map<String, Any?> {
    val javaExt = extensions.findByType(JavaPluginExtension::class.java)
    val pluginClasses = plugins.map { it.javaClass.name }.sorted()

    if (javaExt != null) {
        val sourceSets = javaExt.sourceSets
            .sortedBy { it.name }
            .map { it.toModel(this) }

        return mapOf(
            "projectPath" to path,
            "projectDir" to projectDir.safeCanonicalPath(),
            "kind" to "jvm",
            "plugins" to pluginClasses,
            "sourceSets" to sourceSets
        )
    }

    val hasKotlinExt = extensions.findByName("kotlin") != null

    return mapOf(
        "projectPath" to path,
        "projectDir" to projectDir.safeCanonicalPath(),
        "kind" to if (hasKotlinExt) "kotlin-non-jvm-or-unsupported" else "non-jvm",
        "plugins" to pluginClasses,
        "sourceSets" to emptyList<Map<String, Any?>>(),
        "skipReason" to if (hasKotlinExt) {
            "Kotlin extension exists, but JavaPluginExtension/sourceSets do not. This is likely MPP/JS/Native/Android and needs a separate extractor."
        } else {
            "No JavaPluginExtension/sourceSets on this project."
        }
    )
}

gradle.projectsEvaluated {
    rootProject.tasks.register("dumpKotlinLspModel") {
        group = "kotlin lsp"
        description = "Dumps a JSON model for JVM/Kotlin-JVM projects in this build"

        doLast {
            val projectsModel = rootProject.allprojects
                .sortedBy { it.path }
                .map { it.detectModel() }

            val result = mapOf(
                "rootProject" to rootProject.projectDir.safeCanonicalPath(),
                "projects" to projectsModel
            )

            println("KLSPW_BEGIN")
            println(JsonOutput.prettyPrint(JsonOutput.toJson(result)))
            println("KLSPW_END")
        }
    }
}
