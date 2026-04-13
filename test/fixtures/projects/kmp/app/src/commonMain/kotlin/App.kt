package com.example.kmp.app

import com.example.kmp.core.Greeter
import com.example.kmp.core.createGreeter

class App {
  private val greeter: Greeter = createGreeter()

  fun run(name: String): String = greeter.greet(name)
}
