import com.google.common.base.Joiner

fun greet(names: List<String>): String =
    Joiner.on(", ").join(names)

fun main() {
    println(greet(listOf("Alice", "Bob")))
}
