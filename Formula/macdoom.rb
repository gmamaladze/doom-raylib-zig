class Macdoom < Formula
  desc "DOOM for macOS — the 1993 classic built with Raylib and Zig"
  homepage "https://github.com/gmamaladze/doom-raylib-zig"
  url "https://github.com/gmamaladze/doom-raylib-zig/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "15c1ba998b3b8efff1a65d10a1f5ad54dd48bc4e8ed6f8541f7743a3ddbc1453"
  version "0.1.0"
  license "GPL-2.0-only"

  depends_on "zig" => :build
  depends_on "raylib"
  depends_on :macos

  def install
    cd "macdoom" do
      system "zig", "build",
             "-Doptimize=ReleaseFast",
             "-Draylib-include-path=#{Formula["raylib"].opt_include}",
             "-Draylib-lib-path=#{Formula["raylib"].opt_lib}"
      bin.install "zig-out/bin/macdoom"
    end

    # Create a standard WAD directory
    (share/"doom").mkpath
  end

  def caveats
    <<~EOS
      macdoom requires DOOM WAD files to play. Place your WAD files in:
        ~/.local/share/doom/
      or
        #{share}/doom/
      or set the DOOMWADDIR environment variable:
        export DOOMWADDIR=/path/to/your/wads

      The shareware doom1.wad is freely available online.
    EOS
  end

  test do
    # Verify binary runs (will exit with error since no WAD is present)
    assert_match "DOOM", shell_output("#{bin}/macdoom --version 2>&1", 1)
  end
end
