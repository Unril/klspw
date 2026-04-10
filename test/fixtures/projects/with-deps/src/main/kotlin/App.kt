import okhttp3.HttpUrl.Companion.toHttpUrl

fun buildUrl(base: String, path: String): String =
    base.toHttpUrl().newBuilder().addPathSegment(path).build().toString()

fun main() {
    println(buildUrl("https://example.com", "api"))
}
