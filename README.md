# Audio EVL driver

EVL based real-time audio driver for TI PCM5122/PCM1863 codec for Elk Audio OS..

## Building

Have the kernel sources and an ARMv8 cross-compilation toolchain on your host machine, then do:

(https://github.com/elk-audio/elkpi-sdk) available on the host machine, then:

```
$ export KERNEL_PATH=<path to kernel source tree>
$ export CROSS_COMPILE=<arm compiler prefix>
$ make
```

It's possible to just use the official [Elk Audio OS cross-compiling SDK](https://github.com/elk-audio/elkpi-sdk), in which case you don't have to set the `CROSS_COMPILE` environment variable since the SDK will do it automatically.

As an alternative, you can build using the devshell option of Bitbake if you have all the Yocto layers ready on the host machine:

```
$ bitbake -c devshell virtual/kernel
$ export KERNEL_PATH=<path to kernel source in your bitbake tmp build files>
$ make
```

## Usage Example
To load the driver as an out-of-tree module, run as sudo:

```
$ insmod pcm5122-elk.ko
$ insmod pcm1863-elk.ko
$ insmod bcm2835-i2s-elk.ko
$ insmod audio_evl.ko audio_buffer_size=<BUFFER SIZE>
```

If the modules are installed already as part of the Kernel you can just do instead:

```
 $ modprobe audio_evl audio_buffer_size=<BUFFER SIZE>
```

---
Copyright 2017-2024 ELK Audio AB, Stockholm, Sweden

