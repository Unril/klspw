package com.example.kmp.core

interface Greeter {
  fun greet(name: String): String
}

expect fun createGreeter(): Greeter
