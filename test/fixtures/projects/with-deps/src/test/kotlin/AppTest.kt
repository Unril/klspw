import kotlin.test.Test
import kotlin.test.assertEquals

class AppTest {
    @Test
    fun `builds url with path segment`() {
        assertEquals("https://example.com/api", buildUrl("https://example.com", "api"))
    }
}
