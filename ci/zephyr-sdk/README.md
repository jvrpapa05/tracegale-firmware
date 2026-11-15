# Zephyr SDK checksums

`scripts/ci/setup-zephyr.sh` checks every Zephyr SDK archive it downloads against the file here for that SDK version, and
refuses a version that has none. The SDK's own `sha256.sum` comes from the same release as the archives, so it catches a
damaged download but not a replaced one; these files, kept in this repository, catch both.

To add a version, take the lines for the archives the script uses from that release's `sha256.sum`, after checking the
release is the one the Zephyr project published:

```sh
gh release download v<version> --repo zephyrproject-rtos/sdk-ng --pattern sha256.sum
grep -E 'minimal|hosttools|arm-zephyr-eabi|riscv64-zephyr-elf' sha256.sum > ci/zephyr-sdk/<version>.sha256
```
