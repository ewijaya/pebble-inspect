# Install Inspect from an AWS development server

The installable stock-firmware bundle is `artifacts/pebble-inspect-0.1.1.pbw`.
Check it before transferring:

```sh
cd artifacts
sha256sum -c pebble-inspect-0.1.1.pbw.sha256
```

Target: current Core Devices Pebble Time 2, platform `emery`. This bundle does
not target original Pebble Time (`basalt`). Installation uses the ordinary
Pebble phone app; running Inspect requires no account, backend, subscription,
phone code, or internet connection.

## Cloud developer connection

The phone and CLI sign into the **same Pebble account**. On the phone, connect
the watch, open Devices → watch's three-dot menu → enable **Dev Connection**.
The phone should show “Connected to CloudPebble.” The AWS server reaches it
through the outbound cloud proxy, so no private phone LAN route is needed.

Verified CLI 5.0.40 commands (ping and install subsequently succeeded on
Edward's PT2 after his explicit installation request):

```sh
pebble login --status
timeout 30s pebble ping --cloudpebble
timeout 120s pebble install --cloudpebble artifacts/pebble-inspect-0.1.1.pbw
timeout 60s pebble logs --cloudpebble --no-color
```

If credentials are already valid, keep them. A “Waiting for phone” timeout can
mean the phone app or Dev Connection is inactive; it does not establish that
login expired. Remove conflicting `PEBBLE_PHONE`, `PEBBLE_QEMU`, or
`PEBBLE_EMULATOR` overrides in the installing shell if you set them previously.

For first login on a headless server, the installed CLI supports
`--no-open-browser`, `--auth-host-name`, and `--auth-host-port`. Its OAuth flow
listens on localhost:60000 for a browser callback. From your own computer:

```sh
# Replace YOUR_AWS_HOST with your normal SSH destination; use your usual key.
ssh -L 60000:localhost:60000 ubuntu@YOUR_AWS_HOST
# Inside that SSH session:
pebble login --no-open-browser --auth-host-name localhost --auth-host-port 60000
```

Open the printed URL in the local computer's browser, sign into the same
account as the phone, and leave SSH running until the callback completes.
Then run `pebble login --status`. Do not open port 60000 in a security group,
copy credentials into the repository, or paste tokens into commands stored in
shell history. This task inspected the supported login flow without starting
login; the subsequent authorized physical installation reused the existing login.

## Download and open the PBW

Download through your existing SSH connection, for example on your computer:

```sh
scp ubuntu@YOUR_AWS_HOST:/home/ubuntu/projects/pebble-inspect/artifacts/pebble-inspect-0.1.1.pbw .
scp ubuntu@YOUR_AWS_HOST:/home/ubuntu/projects/pebble-inspect/artifacts/pebble-inspect-0.1.1.pbw.sha256 .
```

Transfer the bundle to the phone and use its supported “Open with” or share
flow to the Pebble app. File association and local installation support depend
on the mobile OS and app version; this route has not been verified on Edward's
phone. Use CloudPebble when the phone does not offer that flow. No public file
hosting or repository publication is required.

## Trusted LAN or VPN

Only if the server can actually route to the phone over a trusted LAN/VPN,
enable the phone's LAN developer connection and use:

```sh
pebble install --phone PHONE_REACHABLE_IP artifacts/pebble-inspect-0.1.1.pbw
pebble logs --phone PHONE_REACHABLE_IP --no-color
```

The service uses port 9000. A phone's private Wi-Fi address is ordinarily
unreachable from AWS. Do not forward that service onto the public internet.

Source: [current Developer Connection guide](https://developer.repebble.com/guides/tools-and-resources/developer-connection/).
All command flags were checked against installed `pebble login --help`,
`install --help`, `logs --help`, and the CLI's local OAuth implementation.
