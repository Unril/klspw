import kotlin.test.Test
import kotlin.test.assertEquals

class AppTest {
    @Test
    fun `greet joins names`() {
        assertEquals("Alice, Bob", greet(listOf("Alice", "Bob")))
    }
}
