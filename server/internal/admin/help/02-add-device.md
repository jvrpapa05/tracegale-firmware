# Add a device

<!-- order: 2 -->
<!-- summary: Give a device its own credentials, and see it come online. -->

Every device has its own certificate and private key, made by Tracegale. Nothing is shared between devices, so if one is lost you
revoke only that one.

## Create the device

1. Open **Devices**, then choose **Add a device**.

![The Devices page. Add a device is at the top right.](images/devices-add.png)

2. Give it a name you'll recognise, such as its location, and choose **Create device**.

![Naming the new device.](images/device-new.png)

3. Tracegale shows the shell commands that give the device its server and credentials.

![The new device's commands. Copy them now: the private key is shown only this once.](images/device-created.png)

> **Copy the commands now.** They hold the device's private key, and the server doesn't keep it. If you lose them, revoke the
> device and add it again.

## Give the device its credentials

4. Open the device's shell, over its serial port or however you reach it.
5. Paste the commands one at a time. Each `tracegale cred` line reports the total so far, so you can check nothing was cut off.
6. After `tracegale commit`, the device connects. Within a few seconds it shows as **Online**.

![The device list, with each device's status, firmware and last reset.](images/devices-list.png)

Open a device to see its firmware version, hardware, build ID and the crashes it has sent.

![A device's page. The outlined details come from the device itself.](images/device-page.png)

Next: [Connect your firmware](03-firmware.md).
