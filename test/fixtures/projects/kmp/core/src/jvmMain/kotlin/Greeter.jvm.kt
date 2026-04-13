package com.example.kmp.core

actual fun createGreeter(): Greeter =
  object : Greeter {
    override fun greet(name: String) = "Hello, $name! (JVM)"
  }
