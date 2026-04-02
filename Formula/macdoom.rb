class Macdoom < Formula
  desc "DOOM for macOS — the 1993 classic built with Raylib and Zig"
  homepage "https://github.com/tornikegomareli/doom-raylib-zig"
  url "https://github.com/tornikegomareli/doom-raylib-zig/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "" # to be filled after tagging tornikegomareli/doom-raylib-zig v0.1.0
  version "0.1.0"
  license "GPL-2.0-only"

  depends_on "zig" => :build
  depends_on "raylib"
  depends_on :macos

  resource "doom1.wad" do
    url "https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad"
    sha256 "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
  end

  def install
    cd "macdoom" do
      system "zig", "build",
             "-Doptimize=ReleaseFast",
             "-Draylib-include-path=#{Formula["raylib"].opt_include}",
             "-Draylib-lib-path=#{Formula["raylib"].opt_lib}"
      bin.install "zig-out/bin/macdoom"
    end

    (share/"doom").install resource("doom1.wad")
  end

  def caveats
    <<~EOS
      The shareware doom1.wad is installed. To play with the full game,
      place your WAD files in:
        ~/.local/share/doom/
      or set the DOOMWADDIR environment variable:
        export DOOMWADDIR=/path/to/your/wads
    EOS
  end

  test do
    # Verify binary runs (will exit with error since no WAD is present)
    assert_match "DOOM", shell_output("#{bin}/macdoom --version 2>&1", 1)
  end
end
