# Security

If you find a way to read a stored password, key or token from the board
without its USB cable, or any other security problem, please don't open a
public issue. Use GitHub's private reporting instead: **Security → Report a
vulnerability** on <https://github.com/ovidiul/sticky-os/security>.

## What to know before you rely on it

- **The USB cable is full access.** The board's flash isn't encrypted and
  secure boot isn't on. Anyone who has the board and a USB cable can read
  everything stored on it (`esptool read_flash`): the WiFi passwords, the
  OpenAI key, the MQTT password and the InfluxDB token, your notes and
  photos. The serial port also takes commands without a password, by design.
  Over the setup protocol (USB or Bluetooth) secrets can be written but are
  never sent back.
- **Bluetooth** is only on in setup mode, which you start on the board. It
  needs the 6-digit code the board shows, and the link is encrypted (LE
  Secure Connections).
- **Releases aren't signed yet.** The board installs only from
  `github.com/ovidiul/sticky-os` releases over https (the certificate is
  checked), checks the size and SHA-256 published with the release and the
  image's own hash, and never installs an older version. But the checksum
  comes from the same release as the image, so it protects against a broken
  download, not against someone who could publish a release on the project.
  That is why **installing updates by itself is off by default**
  (`update.auto`): Settings → Software update shows a new version, and
  Install puts it on.
- **A custom release feed** (`update.url`, for forks) must be https; plain
  http works only in a build made with `-DSTICKY_OTA_ALLOW_HTTP` for testing.
- **Your own services.** `influx.url` may be `http://` and `mqtt.tls` may be
  turned off; the token and the password then travel in clear on your
  network. Use https and TLS unless the network is yours alone.
