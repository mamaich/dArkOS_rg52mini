# Building

    git clone --recursive https://github.com/mamaich/dArkOS_rg52mini
    cd dArkOS_rg52mini
    make rg52mini

The first build takes most of a day; later ones are much shorter because the
Debian rootfs, ccache and every emulator are cached under
`Arkbuild_package_cache/` and `Arkbuild_ccache/`, and `make clean` leaves both
alone. Only `make clean_complete` throws them away.

`BUILD_KODI` defaults to `n`. Pass `BUILD_KODI=y` to include it — but read the
note in KNOWN-ISSUES first, because Kodi's dependency resolution has broken
other components before.

## WSL

Upstream says WSL "will not work due to no support for chroot". That is not
the obstacle. chroot works fine under WSL2; what does not work is binfmt_misc,
and without it every aarch64 binary in the chroot fails with `Exec format
error` — starting with `debootstrap --second-stage`.

Ubuntu ships `/usr/lib/systemd/system/systemd-binfmt.service.d/wsl.conf` with
`ConditionVirtualization=!wsl`, so the qemu handlers are never registered:

    systemd-binfmt.service was skipped because of an unmet condition check
    (ConditionVirtualization=!wsl)

Do not fix this with `systemctl restart systemd-binfmt`. Its `ExecStop`
unregisters everything including `WSLInterop`, which is how Windows binaries
are launched from Linux, and the start half is still skipped. Register the two
handlers directly instead, from a unit of your own:

    :qemu-aarch64:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-aarch64-static:OCF

Flag `F` matters: it opens the interpreter at registration time, so it stays
reachable from inside the chroot.

Two more things WSL needs:

* **btrfs must be loaded before the build starts.** The build filesystem is
  btrfs on a loop device. If the module is not loaded, `mkfs.btrfs` succeeds
  (it is userspace) and `mount` fails — and the build used to carry on for two
  and a half hours writing into a plain directory on the host filesystem
  instead of the image, only failing at `write_rootfs`. `setup_partition-rk3562.sh`
  now checks the mount, but keep `btrfs` in `/etc/modules-load.d/` anyway.
* **`sudo` without a password.** `./FreeSudo.sh` does this. Under WSL you can
  also reach root without one via `wsl -u root`, which is useful when sudo
  itself is what is misconfigured.

`wsl --shutdown` unloads kernel modules, so anything not in `modules-load.d`
has to be reloaded afterwards.

## Things that cost hours, once each

**The toolchain PATH used to be relative.** `utils.sh` put
`prebuilts/gcc/.../bin` on `PATH`, and `build_kernel-rk3562.sh` runs
`make -C kernel_rk3562`. make changes directory first, so the relative entry
then resolved against the kernel tree and the linker vanished:

    scripts/Kconfig.include:40: linker 'aarch64-linux-gnu-ld' not found

Fixed — it is absolute now. It only ever bit when the toolchain was not
installed system-wide in `/opt/toolchains`.

**A Windows reboot kills the build.** Everything else survives: the image, the
caches, the mounts. If the build dies late, the mounts and loop devices are
still in place and the remaining steps can be re-run by hand rather than
starting over — that is how the 2026-09-15 image was finished after
`fetch_compat_libs.sh` aborted in `finishing_touches`.

**Nothing started from `wsl.exe` outlives that invocation.** Not with `nohup`,
not with `setsid`, not with `disown` — WSL kills every process the invocation
spawned, not merely the foreground process group, so the usual detaching tricks
all fail. It is not cgroup-based: every client lands in a shared `/init.scope`
and separate invocations leave each other alone. What survives is a process
whose parent is systemd:

    sudo systemd-run --unit=darkos-build \
         --working-directory=/home/mamaich/rg52/dArkOS_rg52mini \
         --setenv=BUILD_KODI=y make rg52mini

    systemctl status darkos-build
    journalctl -u darkos-build -f

Start a build that way if the terminal — or the editor, or the agent session —
that launched it might not be there in nine hours. A build started the ordinary
way is tied to a live `wsl.exe` client, and closing it is the same as a reboot.
Nothing can be done about it afterwards: the running build has a controlling
terminal and a live parent, and neither can be changed from outside.

**Silent losses.** Most `build_<emu>sa.sh` scripts have no `verify_action`: a
component that fails to patch or clone prints one line among tens of thousands
and the build moves on, leaving an empty `/opt/<name>` behind. `make rg52mini`
now ends with an audit (`scripts/audit-components.sh`) that lists directories
with no ELF in them, every step that gave up, and every package that would not
install. Read it.

**A defconfig is a request, not a result.** Appending `CONFIG_FOO=y` to
`rg52mini_defconfig` does nothing at all if `FOO`'s dependencies are unmet —
kconfig drops it without a word. That is how `CONFIG_LOGO` stayed out of three
images. The reverse bites too: turning something *off* can take an unrelated
option with it, because the thing you disabled was the only one `select`ing it.
`CONFIG_BLK_DEV_IO_TRACE` was the only selector of `CONFIG_DEBUG_FS` in this
tree, and `scripts/rk3562/ledctl.sh` drives the power LED through
`/sys/kernel/debug/regulator/vcc-led/enable`, so switching off blktrace would
have quietly taken out the LED.

Resolve the defconfig and diff it, every time. It can be done without touching
the working tree — useful while a build is running, since `make <unit>_defconfig`
would overwrite the `.config` the build is using, and `make O=<dir>` refuses
outright once the tree has object files in it:

    cd kernel_rk3562
    export ARCH=arm64 SRCARCH=arm64 srctree=. objtree=. \
           CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc \
           HOSTCC=gcc KERNELVERSION=$(make -s kernelversion)
    export PATH=$PWD/../prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin:$PATH
    export CC_VERSION_TEXT="$(aarch64-linux-gnu-gcc --version | head -n1)"
    KCONFIG_CONFIG=/tmp/newcfg scripts/kconfig/conf \
        --defconfig=arch/arm64/configs/rg52mini_defconfig Kconfig
    diff <(grep ^CONFIG_ .config | sort) <(grep ^CONFIG_ /tmp/newcfg | sort)

`scripts/kconfig/conf` is already built after any kernel build, and
`KCONFIG_CONFIG` sends the output somewhere harmless. Export `CC_VERSION_TEXT`
or every compiler probe fails: you get `CONFIG_GCC_VERSION=0`, `GCC_PLUGINS`
disappears and `KASAN` appears, and the diff fills with noise that has nothing
to do with your change. Read the whole diff, not just the symbols you edited.
