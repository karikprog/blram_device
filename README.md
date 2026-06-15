# blram_device

A simple RAM-backed Linux block device driver (kernel module).
The driver registers a 16 MB block device (`/dev/my_blkram_dev`) backed by memory allocated with `vmalloc`, using the `blk-mq` API.

## Build

```bash
make build
```

This produces `blramdev.ko`.

```bash
make clean
```

## Run tests

Tests are not run in CI and must be executed manually on the target machine (root privileges required).

```bash
make build
sudo bash tests/run_tests.sh
```

The script:
- loads the module (`insmod`)
- checks that `/dev/my_blkram_dev` exists
- checks device capacity (16777216 bytes)
- performs a write/read test with `dd`
- performs a boundary write/read test on the last sector
- creates an ext4 filesystem, mounts it, writes and reads a file
- attempts an out-of-bounds write
- unloads the module (`rmmod`)

## Manual testing commands

```bash
# load module
sudo insmod blramdev.ko

# check device node
ls -l /dev/my_blkram_dev

# check capacity
sudo blockdev --getsize64 /dev/my_blkram_dev

# write/read test
sudo dd if=/dev/urandom of=/tmp/in bs=4096 count=16
sudo dd if=/tmp/in of=/dev/my_blkram_dev bs=4096 count=16 conv=notrunc
sudo dd if=/dev/my_blkram_dev of=/tmp/out bs=4096 count=16
cmp /tmp/in /tmp/out

# filesystem test
sudo mkfs.ext4 -F /dev/my_blkram_dev
sudo mount /dev/my_blkram_dev /mnt
echo "hello" | sudo tee /mnt/hello.txt
cat /mnt/hello.txt
sudo umount /mnt

# unload module
sudo rmmod blramdev

# check kernel logs
dmesg | tail -n 30
```

## Code formatting

```bash
clang-format -i *.c
clang-format --dry-run --Werror *.c
```

## Contact

Karamanov Karim — karamanov2007@gmail.com