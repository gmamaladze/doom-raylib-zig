const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Configurable raylib paths (default: Homebrew on Apple Silicon)
    const raylib_include = b.option([]const u8, "raylib-include-path", "Path to raylib headers (default: /opt/homebrew/include)") orelse "/opt/homebrew/include";
    const raylib_lib = b.option([]const u8, "raylib-lib-path", "Path to raylib library (default: /opt/homebrew/lib)") orelse "/opt/homebrew/lib";

    // Create root module
    const root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    // Link system-installed raylib
    root_module.addSystemIncludePath(.{ .cwd_relative = raylib_include });
    root_module.addLibraryPath(.{ .cwd_relative = raylib_lib });
    root_module.linkSystemLibrary("raylib", .{});

    // Link macOS frameworks required by raylib + music playback
    root_module.linkFramework("IOKit", .{});
    root_module.linkFramework("Cocoa", .{});
    root_module.linkFramework("OpenGL", .{});
    root_module.linkFramework("AudioToolbox", .{});
    root_module.linkFramework("CoreFoundation", .{});

    // Add compat headers as system include path (provides values.h, malloc.h shims)
    root_module.addSystemIncludePath(b.path("src/compat"));

    // Add original DOOM source as include path (for headers)
    root_module.addIncludePath(.{
        .cwd_relative = "../linuxdoom-1.10",
    });

    // Common C flags for DOOM source
    const doom_cflags = &.{
        "-DNORMALUNIX",
        "-Wno-implicit-function-declaration",
        "-Wno-pointer-sign",
        "-Wno-implicit-int",
        "-Wno-return-type",
        "-Wno-unused-result",
        "-Wno-int-conversion",
        "-Wno-parentheses",
        "-Wno-shift-negative-value",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
        "-fno-sanitize=undefined",
    };

    // Original DOOM game logic source files (from linuxdoom-1.10/)
    // These are ALL the .c files EXCEPT the 5 i_*.c platform files
    root_module.addCSourceFiles(.{
        .root = .{ .cwd_relative = "../linuxdoom-1.10" },
        .files = &.{
            "doomdef.c",
            "doomstat.c",
            "dstrings.c",
            "tables.c",
            "f_finale.c",
            "f_wipe.c",
            "d_main.c",
            "d_net.c",
            "d_items.c",
            // g_game.c compiled via compat (debug logging)
            "m_menu.c",
            // m_misc.c compiled via wrapper (see src/compat/m_misc_wrapper.c)
            "m_argv.c",
            "m_bbox.c",
            "m_fixed.c",
            "m_swap.c",
            "m_cheat.c",
            "m_random.c",
            "am_map.c",
            "p_ceilng.c",
            "p_doors.c",
            "p_enemy.c",
            "p_floor.c",
            "p_inter.c",
            "p_lights.c",
            "p_map.c",
            "p_maputl.c",
            "p_plats.c",
            "p_pspr.c",
            // p_setup.c compiled via compat (64-bit linebuffer fix)
            "p_sight.c",
            "p_spec.c",
            "p_switch.c",
            "p_mobj.c",
            "p_telept.c",
            "p_tick.c",
            "p_saveg.c",
            "p_user.c",
            "r_bsp.c",
            // r_data.c compiled via compat (64-bit maptexture_t fix)
            // r_draw.c compiled via compat (64-bit pointer alignment fix)
            "r_main.c",
            "r_plane.c",
            "r_segs.c",
            "r_sky.c",
            "r_things.c",
            "w_wad.c",
            "wi_stuff.c",
            "v_video.c",
            "st_lib.c",
            "st_stuff.c",
            "hu_stuff.c",
            "hu_lib.c",
            "s_sound.c",
            // z_zone.c compiled via compat (64-bit alignment fix)
            "info.c",
            "sounds.c",
        },
        .flags = doom_cflags,
    });

    // New platform files (Raylib-based)
    root_module.addCSourceFiles(.{
        .root = b.path("src/platform"),
        .files = &.{
            "i_main_rl.c",
            "i_system_rl.c",
            "i_video_rl.c",
            "i_sound_rl.c",
            "i_net_rl.c",
        },
        .flags = doom_cflags,
    });

    // Compat replacements (files patched for 64-bit compatibility)
    root_module.addCSourceFiles(.{
        .root = b.path("src/compat"),
        .files = &.{
            "m_misc_compat.c",
            "z_zone_compat.c",
            "r_data_compat.c",
            "p_setup_compat.c",
            "r_draw_compat.c",
            "g_game_compat.c",
        },
        .flags = doom_cflags,
    });

    // Create executable
    const exe = b.addExecutable(.{
        .name = "macdoom",
        .root_module = root_module,
    });

    b.installArtifact(exe);

    // Run step: zig build run -- [args]
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run DOOM");
    run_step.dependOn(&run_cmd.step);
}
