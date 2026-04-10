class Klspw < Formula
  desc "Generate workspace.json for kotlin-lsp from Gradle builds"
  homepage "https://github.com/Unril/klspw"
  url "https://github.com/Unril/klspw/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "TODO_REPLACE_WITH_ACTUAL_SHA256"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "cli11"
  depends_on "glaze"
  depends_on "reproc"
  depends_on "spdlog"

  def install
    system "cmake", "-S", ".", "-B", "build",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    # init subcommand generates a starter config for a Gradle root
    (testpath/"fake-root").mkpath
    (testpath/"fake-root/build.gradle.kts").write("")
    output = shell_output("#{bin}/klspw init #{testpath}/fake-root")
    assert_match "roots:", output

    # --version should print the version
    assert_match version.to_s, shell_output("#{bin}/klspw --version")
  end
end
