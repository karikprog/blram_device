#!/bin/bash
# Simple integration tests for my_blkram_dev (Fedora, kernel 6+)
set -e

MODULE=blramdev
DEVNAME=my_blkram_dev
DEVPATH=/dev/$DEVNAME
CAPACITY=16777216

cleanup() {
    echo "=== Cleanup ==="
    sudo rmmod $MODULE 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Loading module ==="
sudo insmod ${MODULE}.ko
sleep 1

echo "=== Checking device node exists ==="
if [ ! -b "$DEVPATH" ]; then
    echo "FAIL: $DEVPATH not found"
    exit 1
fi
echo "PASS: device node exists"

echo "=== Checking capacity (expect $CAPACITY bytes) ==="
SIZE=$(sudo blockdev --getsize64 $DEVPATH)
if [ "$SIZE" -ne "$CAPACITY" ]; then
    echo "FAIL: size is $SIZE, expected $CAPACITY"
    exit 1
fi
echo "PASS: size is correct ($SIZE bytes)"

echo "=== Write/Read test (dd) ==="
TESTDATA="/tmp/blkram_test_in"
RESULTDATA="/tmp/blkram_test_out"
dd if=/dev/urandom of=$TESTDATA bs=4096 count=16 status=none

sudo dd if=$TESTDATA of=$DEVPATH bs=4096 count=16 conv=notrunc status=none
sync
sudo dd if=$DEVPATH of=$RESULTDATA bs=4096 count=16 status=none

if cmp -s $TESTDATA $RESULTDATA; then
    echo "PASS: write/read data matches"
else
    echo "FAIL: data mismatch after write/read"
    exit 1
fi
rm -f $TESTDATA $RESULTDATA

echo "=== Boundary write test (last sector) ==="
OFFSET=$((CAPACITY - 512))
echo -n "boundarytest" > /tmp/blkram_boundary
sudo dd if=/tmp/blkram_boundary of=$DEVPATH bs=512 seek=$((OFFSET / 512)) count=1 conv=notrunc status=none
sync
sudo dd if=$DEVPATH of=/tmp/blkram_boundary_out bs=512 skip=$((OFFSET / 512)) count=1 status=none

if cmp -s <(head -c 12 /tmp/blkram_boundary) <(head -c 12 /tmp/blkram_boundary_out); then
    echo "PASS: boundary write/read matches"
else
    echo "FAIL: boundary write/read mismatch"
    exit 1
fi
rm -f /tmp/blkram_boundary /tmp/blkram_boundary_out

echo "=== Filesystem test (mkfs + mount) ==="
sudo mkfs.ext4 -q -F $DEVPATH
MNTDIR=$(mktemp -d)
sudo mount $DEVPATH $MNTDIR
echo "hello ramdisk" | sudo tee $MNTDIR/hello.txt > /dev/null
sudo cat $MNTDIR/hello.txt | grep -q "hello ramdisk"
sudo umount $MNTDIR
rmdir $MNTDIR
echo "PASS: filesystem mount/write/read works"

echo "=== Out-of-bounds I/O test ==="
SECTORS=$((CAPACITY / 512))
sudo dd if=/dev/zero of=$DEVPATH bs=512 seek=$SECTORS count=1 conv=notrunc status=none 2>/dev/null && \
    echo "WARN: out-of-bounds write did not error" || \
    echo "PASS: out-of-bounds write rejected"

echo "=== Unloading module ==="
sudo rmmod $MODULE

echo "=== All tests completed ==="